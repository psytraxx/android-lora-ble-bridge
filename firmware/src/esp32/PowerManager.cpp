#include "esp32/PowerManager.h"
#include "esp32/FirmwareConfig.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <driver/rtc_io.h>
#include <soc/rtc.h>
#include <esp_pm.h>

// Battery voltage to percentage lookup table
const float MIN_VOLTAGE = 3.0;
const float MAX_VOLTAGE = 4.2;

void PowerManager::configurePowerManagement()
{

    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = PowerConstants::CPU_MIN_FREQ_MHZ,
        .light_sleep_enable = false}; // Disable light sleep to avoid issues with peripherals

    esp_err_t rv = esp_pm_configure(&pm_config);
    if (rv != ESP_OK)
    {
        Serial.printf("Failed to configure power management (err=%d)\n", rv);
        return;
    }

    // Configure battery ADC pin
#ifdef BATTERY_ADC_PIN
    pinMode(BATTERY_ADC_PIN, INPUT);

#ifdef BATTERY_ADC_CTRL
    // Enable battery voltage reading (Heltec boards)
    pinMode(BATTERY_ADC_CTRL, OUTPUT);
    digitalWrite(BATTERY_ADC_CTRL, LOW); // Enable ADC reading
#endif
#endif

    // Start with low power CPU frequency (will scale up for radio operations)
    setCpuLowPower();
    Serial.println("Power management configured (CPU: 80MHz low-power mode)");
}

float PowerManager::readBatteryVoltage()
{
#ifdef BATTERY_ADC_PIN
    // Read ADC value
    int rawValue = analogRead(BATTERY_ADC_PIN);

    // Convert to voltage (ESP32 ADC is 12-bit, 0-4095, reference ~3.3V)
    float voltage = (rawValue / 4095.0) * 3.3;

    // Apply voltage divider scaling
#ifdef BATTERY_VOLTAGE_DIVIDER
    voltage *= BATTERY_VOLTAGE_DIVIDER;
#endif

    return voltage;
#else
    return 3.7; // Default voltage if no ADC configured
#endif
}

uint8_t PowerManager::readBatteryLevel()
{
    float voltage = readBatteryVoltage();

    // Simple linear mapping from voltage to percentage
    if (voltage >= MAX_VOLTAGE)
    {
        return 100;
    }
    else if (voltage <= MIN_VOLTAGE)
    {
        return 0;
    }
    else
    {
        return (uint8_t)(((voltage - MIN_VOLTAGE) / (MAX_VOLTAGE - MIN_VOLTAGE)) * 100.0);
    }
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

void PowerManager::setCpuLowPower()
{
    // Reduce CPU frequency to 80MHz for power savings
    setCpuFrequencyMhz(80);
}

void PowerManager::setCpuFullPower()
{
    // Restore full CPU frequency (240MHz) for radio operations
    setCpuFrequencyMhz(240);
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
