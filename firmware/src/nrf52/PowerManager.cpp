#include "nrf52/PowerManager.h"
#include "nrf52/FirmwareConfig.h"
#include <Arduino.h>

// nRF52 power management includes
#ifdef ARDUINO_ARCH_NRF52
#include "nrf_power.h"
#include "nrf_gpio.h"
#endif

PowerManager::PowerManager()
    : lastBatteryLevel(100),
      initial_read_done(false),
      last_read_value(3300.0), // Start at reasonable 3.3V
      last_read_time_ms(0)
{
}

bool PowerManager::begin()
{
    // Configure ADC for battery monitoring
    analogReadResolution(PowerConstants::ADC_RESOLUTION_BITS);
    analogReference(AR_INTERNAL); // Use internal 0.6V reference

#ifdef BATTERY_ADC_CTRL
    // Initialize VBAT_ENABLE to HIGH (disabled state) to save power
    // Enable only when reading battery voltage
    pinMode(BATTERY_ADC_CTRL, OUTPUT);
    digitalWrite(BATTERY_ADC_CTRL, HIGH);
    Serial.printf("Battery ADC control pin (GPIO %d) initialized to disabled\n", BATTERY_ADC_CTRL);
#endif

#ifdef ARDUINO_ARCH_NRF52
    // Enable DC/DC converter for better power efficiency
    // This can reduce power consumption by 20-30%
    if (nrf_power_dcdcen_get(NRF_POWER) == false)
    {
        nrf_power_dcdcen_set(NRF_POWER, true);
        Serial.println("DC/DC regulator enabled (20-30% power savings)");
    }
    else
    {
        Serial.println("DC/DC regulator already enabled");
    }
#endif

    Serial.println("PowerManager initialized");
    return true;
}

void PowerManager::battery_adcEnable()
{
#ifdef BATTERY_ADC_CTRL
    // Enable battery voltage divider (set VBAT_ENABLE LOW)
    pinMode(BATTERY_ADC_CTRL, OUTPUT);
    digitalWrite(BATTERY_ADC_CTRL, LOW);
    delay(10); // Wait for voltage to stabilize
#endif
}

void PowerManager::battery_adcDisable()
{
#ifdef BATTERY_ADC_CTRL
    // Disable battery voltage divider to save power (set VBAT_ENABLE HIGH)
    digitalWrite(BATTERY_ADC_CTRL, HIGH);
#endif
}

uint8_t PowerManager::voltageToPercentage(uint16_t voltagePerCellMv)
{
    // OCV lookup table from FirmwareConfig
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

uint16_t PowerManager::readBatteryVoltage()
{
    // Throttle ADC reads to once per 5 seconds minimum
    const uint32_t MIN_READ_INTERVAL_MS = 5000;
    const uint32_t BATTERY_SENSE_SAMPLES = 10;

    if (!initial_read_done || (millis() - last_read_time_ms >= MIN_READ_INTERVAL_MS))
    {
        last_read_time_ms = millis();

        // Enable battery ADC (set VBAT_ENABLE LOW on Seeed XIAO)
        battery_adcEnable();

        // Read battery voltage from ADC with averaging
        // XIAO nRF52840: Battery voltage may be divided by hardware
        uint32_t adcSum = 0;
        for (uint32_t i = 0; i < BATTERY_SENSE_SAMPLES; i++)
        {
            adcSum += analogRead(BATTERY_ADC_PIN);
        }
        int adcValue = adcSum / BATTERY_SENSE_SAMPLES;

        // Disable battery ADC to save power (set VBAT_ENABLE HIGH)
        battery_adcDisable();

        // Convert ADC value to voltage in millivolts
        // nRF52840: 12-bit ADC (0-4095) with 0.6V internal reference and 1/6 gain
        // Max measurable voltage = 0.6V * 6 = 3.6V at ADC value 4095
        // voltage_mv = (adcValue / 4095.0) * 3600.0 * BATTERY_DIVIDER
        float voltage_mv = (adcValue / 4095.0) * PowerConstants::ADC_MAX_VOLTAGE *
                           PowerConstants::BATTERY_DIVIDER * 1000.0;

        if (!initial_read_done)
        {
            // Initialize filter with first reading if plausible
            if (voltage_mv > last_read_value)
            {
                last_read_value = voltage_mv;
            }
            initial_read_done = true;
        }
        else
        {
            // Apply low-pass filter: output = output + alpha * (input - output)
            // Alpha = 0.5 provides good balance between responsiveness and smoothing
            last_read_value += (voltage_mv - last_read_value) * 0.5;
        }

        // Log debug info periodically
        static uint32_t last_log = 0;
        if (millis() - last_log > 30000)
        {
            Serial.printf("Battery: adc=%d, voltage=%u mV, filtered=%u mV\n",
                          adcValue, (uint16_t)voltage_mv, (uint16_t)last_read_value);
            last_log = millis();
        }
    }

    return (uint16_t)last_read_value;
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
        lastBatteryLevel = 0; // No battery or critically low
        return 0;
    }

    // Convert voltage to percentage using OCV lookup table
    lastBatteryLevel = voltageToPercentage(voltage);
    return lastBatteryLevel;
}

void PowerManager::enterLowPowerMode()
{
    Serial.println("Entering System OFF mode...");

#ifdef ARDUINO_ARCH_NRF52
    // Configure LoRa DIO0 interrupt pin as wake-up source
    // DIO0 goes HIGH when packet is received (matches ESP32 configuration)
    pinMode(LORA_DIO0, INPUT_SENSE_HIGH);
    nrf_gpio_cfg_sense_input(LORA_DIO0, NRF_GPIO_PIN_PULLDOWN, NRF_GPIO_PIN_SENSE_HIGH);
    Serial.printf("Wake source: LoRa DIO0 (GPIO %d) - wake on HIGH\n", LORA_DIO0);

    // Configure wake button pin mode before sense setup
    pinMode(WAKE_BUTTON, INPUT_PULLUP);
    // Configure wake button as wake-up source (wake on LOW)
    nrf_gpio_cfg_sense_input(WAKE_BUTTON, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
    Serial.printf("Wake source: Wake button (GPIO %d) - wake on LOW\n", WAKE_BUTTON);

    // Flush serial output
    Serial.flush();
    delay(100); // Allow time for serial transmission

    // Enter System OFF mode (lowest power state)
    // Current: ~0.2µA (all RAM and peripherals powered down)
    // Device will perform full reset on wake (execution starts from setup())
    //
    // Note: Use sd_power_system_off() if SoftDevice (BLE stack) is active
    // For now, we shut down BLE before entering sleep, so direct register access is safe
    NRF_POWER->SYSTEMOFF = 1;

    // This line should never be reached
    Serial.println("ERROR: Failed to enter System OFF mode");
#else
    Serial.println("System OFF mode only available on nRF52");
#endif
}
