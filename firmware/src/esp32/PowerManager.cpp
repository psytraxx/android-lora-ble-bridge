#include "esp32/PowerManager.h"
#include "esp32/FirmwareConfig.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <driver/rtc_io.h>
#include <soc/rtc.h>
#include <esp_pm.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

// Static variables for ADC calibration and filtering
static esp_adc_cal_characteristics_t *adc_characs = nullptr;
static bool initial_read_done = false;
static float last_read_value = 3300.0; // Start at reasonable 3.3V
static uint32_t last_read_time_ms = 0;

void PowerManager::configurePowerManagement()
{
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 80,
        .light_sleep_enable = false}; // Disable light sleep to avoid issues with peripherals

    esp_err_t rv = esp_pm_configure(&pm_config);
    if (rv != ESP_OK)
    {
        Serial.printf("Failed to configure power management (err=%d) - continuing anyway\n", rv);
        setCpuFrequencyMhz(160); // Fallback to fixed 160 MHz
        // Don't return early - ADC calibration still needs to be initialized
    }
    else
    {
        Serial.printf("Power management configured: CPU freq %d-%d MHz\n",
                      pm_config.min_freq_mhz,
                      pm_config.max_freq_mhz);
    }

#ifdef BATTERY_ADC_PIN
    // Initialize ADC calibration for battery voltage reading
    adc_characs = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));

    // Configure ADC (channel is determined from BATTERY_ADC_PIN at runtime)
    adc1_config_width(ADC_WIDTH_BIT_12);

    // Determine ADC channel from GPIO pin number
    adc1_channel_t adc_channel;
    if (BATTERY_ADC_PIN == 1)
    {
        adc_channel = ADC1_CHANNEL_0; // GPIO1 = ADC1_CH0 on ESP32-S3
    }
    else if (BATTERY_ADC_PIN == 4)
    {
        adc_channel = ADC1_CHANNEL_3; // GPIO4 = ADC1_CH3 on ESP32-S3
    }
    else
    {
        Serial.printf("WARNING: Unknown ADC channel for GPIO %d\n", BATTERY_ADC_PIN);
        adc_channel = ADC1_CHANNEL_0; // Fallback
    }

    adc1_config_channel_atten(adc_channel, ADC_ATTEN_DB_2_5);

    // Calibrate ADC using eFuse values
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_DB_2_5,
        ADC_WIDTH_BIT_12,
        1100, // Default Vref
        adc_characs);

    // Log calibration type
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP)
    {
        Serial.println("ADC calibration: Two Point values from eFuse");
    }
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF)
    {
        Serial.println("ADC calibration: Vref from eFuse");
    }
    else
    {
        Serial.println("ADC calibration: Default Vref");
    }
#endif

    Serial.println("Power management configured");
}

void PowerManager::battery_adcEnable()
{
#ifdef BATTERY_ADC_CTRL
    // Enable ADC voltage divider (Heltec boards)
    pinMode(BATTERY_ADC_CTRL, INPUT);
    uint8_t adc_ctl_enable_value = !(digitalRead(BATTERY_ADC_CTRL));
    pinMode(BATTERY_ADC_CTRL, OUTPUT);
    digitalWrite(BATTERY_ADC_CTRL, adc_ctl_enable_value);
    delay(10); // Wait for voltage to stabilize
#endif
}

void PowerManager::battery_adcDisable()
{
#ifdef BATTERY_ADC_CTRL
    // Disable ADC voltage divider to save power
    pinMode(BATTERY_ADC_CTRL, ANALOG);
#endif
}

uint32_t PowerManager::espAdcRead()
{
#ifdef BATTERY_ADC_PIN
    const uint32_t BATTERY_SENSE_SAMPLES = 15;
    uint32_t raw = 0;
    uint8_t raw_c = 0; // Valid reading counter

    // Determine ADC channel from GPIO pin
    adc1_channel_t adc_channel;
    if (BATTERY_ADC_PIN == 1)
    {
        adc_channel = ADC1_CHANNEL_0;
    }
    else if (BATTERY_ADC_PIN == 4)
    {
        adc_channel = ADC1_CHANNEL_3;
    }
    else
    {
        adc_channel = ADC1_CHANNEL_0;
    }

    // Take multiple samples and average
    for (uint32_t i = 0; i < BATTERY_SENSE_SAMPLES; i++)
    {
        int val = adc1_get_raw(adc_channel);
        if (val >= 0)
        {
            raw += val;
            raw_c++;
        }
    }

    return (raw_c < 1) ? 0 : (raw / raw_c);
#else
    return 0;
#endif
}

uint16_t PowerManager::readBatteryVoltage()
{
#ifdef BATTERY_ADC_PIN
    // Check if ADC calibration is initialized
    if (adc_characs == nullptr)
    {
        Serial.println("WARNING: ADC not calibrated, returning default voltage");
        return 3700; // Default 3.7V
    }

    // Throttle ADC reads to once per 5 seconds minimum
    const uint32_t MIN_READ_INTERVAL_MS = 5000;

    if (!initial_read_done || (millis() - last_read_time_ms >= MIN_READ_INTERVAL_MS))
    {
        last_read_time_ms = millis();

        // Enable ADC voltage divider
        battery_adcEnable();

        // Read calibrated ADC value with averaging
        uint32_t raw = espAdcRead();

        // Convert to voltage using calibration
        uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(raw, adc_characs);

        // Apply voltage divider ratio
        float scaled = voltage_mv * BATTERY_VOLTAGE_DIVIDER;

        // Disable ADC to save power
        battery_adcDisable();

        if (!initial_read_done)
        {
            // Initialize filter with first reading if plausible
            if (scaled > last_read_value)
            {
                last_read_value = scaled;
            }
            initial_read_done = true;
        }
        else
        {
            // Apply low-pass filter: output = output + alpha * (input - output)
            // Alpha = 0.5 provides good balance between responsiveness and smoothing
            last_read_value += (scaled - last_read_value) * 0.5;
        }

        // Log debug info periodically
        static uint32_t last_log = 0;
        if (millis() - last_log > 30000)
        {
            Serial.printf("Battery: raw=%u, cal=%u mV, scaled=%u mV, filtered=%u mV\n",
                          raw, voltage_mv, (uint32_t)scaled, (uint32_t)last_read_value);
            last_log = millis();
        }
    }

    return (uint16_t)last_read_value;
#else
    return 3700; // Default 3.7V if no ADC
#endif
}

uint8_t PowerManager::voltageToPercentage(uint16_t voltagePerCellMv)
{
    // OCV lookup table from PowerConstants namespace
    extern const uint16_t OCV[];
    extern const int NUM_OCV_POINTS;

    float battery_SOC = 0.0;

    // Find the OCV range and interpolate
    for (int i = 0; i < NUM_OCV_POINTS; i++)
    {
        if (OCV[i] <= voltagePerCellMv)
        {
            if (i == 0)
            {
                battery_SOC = 100.0; // Fully charged
            }
            else
            {
                // Linear interpolation between OCV[i] and OCV[i-1]
                battery_SOC = (float)100.0 / (NUM_OCV_POINTS - 1.0) *
                              (NUM_OCV_POINTS - 1.0 - i +
                               ((float)voltagePerCellMv - OCV[i]) / (OCV[i - 1] - OCV[i]));
            }
            break;
        }
    }

    // Clamp to 0-100 range
    if (battery_SOC > 100.0)
        battery_SOC = 100.0;
    if (battery_SOC < 0.0)
        battery_SOC = 0.0;

    return (uint8_t)battery_SOC;
}

uint8_t PowerManager::readBatteryLevel()
{
    uint16_t voltage = readBatteryVoltage();

    // Check for no battery condition
    extern const uint16_t OCV[];
    extern const int NUM_OCV_POINTS;
    const uint16_t MIN_BATTERY_VOLTAGE = OCV[NUM_OCV_POINTS - 1] - 500;

    if (voltage < MIN_BATTERY_VOLTAGE)
    {
        return 0; // No battery or critically low
    }

    // Convert voltage to percentage using OCV lookup table
    // Assuming single cell battery (NUM_CELLS = 1)
    return voltageToPercentage(voltage);
}

void PowerManager::disableWiFi()
{
    // Disable WiFi and free resources
    esp_wifi_stop();
    esp_wifi_deinit();
    Serial.println("WiFi disabled to save power");
}

void PowerManager::disableBluetoothClassic()
{
    // Disable Bluetooth Classic (BLE is handled separately by NimBLE)
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    Serial.println("Bluetooth Classic disabled");
}

void PowerManager::configureWakeupSources(int wakeButton, int loraDio0)
{
    Serial.println("Configuring deep sleep wake sources...");

    // Configure LoRa DIO0 as EXT0 wake source (wake on HIGH level)
    // When LoRa receives a packet, DIO0 goes HIGH and wakes the device
    esp_sleep_enable_ext0_wakeup((gpio_num_t)loraDio0, HIGH);
    Serial.printf("  EXT0: LoRa DIO0 (GPIO %d) - wake on HIGH\n", loraDio0);

    // Configure wake button as EXT1 wake source (wake on LOW level)
    // Button pressed = LOW (with internal pull-up)
    uint64_t buttonMask = 1ULL << wakeButton;
    esp_sleep_enable_ext1_wakeup(buttonMask, ESP_EXT1_WAKEUP_ANY_LOW);
    Serial.printf("  EXT1: Wake button (GPIO %d) - wake on LOW\n", wakeButton);

    // Enable RTC GPIO for wake sources
    rtc_gpio_pullup_en((gpio_num_t)wakeButton);
    rtc_gpio_pulldown_dis((gpio_num_t)wakeButton);

    rtc_gpio_pulldown_en((gpio_num_t)loraDio0);
    rtc_gpio_pullup_dis((gpio_num_t)loraDio0);

    Serial.println("Wake sources configured");
}

void PowerManager::disableExternalPeripherals()
{
#ifdef VEXT_PIN
    // Set VEXT to input mode to cut power to external peripherals
    pinMode(VEXT_PIN, INPUT);
    Serial.println("External peripherals disabled (VEXT)");
#endif
}

void PowerManager::setUnusedGPIOsToInput()
{
    // Set unused GPIOs to input mode to minimize leakage current
    // This is a simplified version - in production, carefully review which pins to isolate
    Serial.println("Setting unused GPIOs to input mode for power savings");

    // Note: Be careful not to set pins that are actively used:
    // - LoRa SPI pins (LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS)
    // - LoRa control pins (LORA_RST, LORA_DIO0, LORA_BUSY)
    // - Battery ADC (BATTERY_ADC_PIN, BATTERY_ADC_CTRL)
    // - Wake button (WAKE_BUTTON)
    // - LED (LED_PIN)
    // - VEXT (VEXT_PIN)
}

void PowerManager::enterDeepSleep()
{
    Serial.println("Entering deep sleep...");

    // Disable external peripherals to save power
    disableExternalPeripherals();

    // Set unused GPIOs to input
    setUnusedGPIOsToInput();

    // Flush serial output to ensure logs are sent
    Serial.flush();

    // Enter deep sleep (does not return - device resets on wake)
    esp_deep_sleep_start();
}

void PowerManager::printWakeupReason()
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    Serial.print("Wakeup reason: ");
    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_EXT0:
        Serial.println("EXT0 (LoRa DIO0)");
        break;
    case ESP_SLEEP_WAKEUP_EXT1:
        Serial.println("EXT1 (Wake button)");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        Serial.println("Timer");
        break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        Serial.println("Touchpad");
        break;
    case ESP_SLEEP_WAKEUP_ULP:
        Serial.println("ULP coprocessor");
        break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
        Serial.println("Power-on reset or other");
        break;
    }
}
