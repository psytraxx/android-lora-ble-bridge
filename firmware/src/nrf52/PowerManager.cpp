#include "nrf52/PowerManager.h"
#include "nrf52/FirmwareConfig.h"
#include <Arduino.h>

// nRF52 power management includes
#ifdef ARDUINO_ARCH_NRF52
#include "nrf_power.h"
#include "nrf_gpio.h"
#endif

PowerManager::PowerManager()
    : lastBatteryLevel(100)
{
}

bool PowerManager::begin()
{
    // Configure ADC for battery monitoring
    analogReadResolution(PowerConstants::ADC_RESOLUTION_BITS);
    analogReference(AR_INTERNAL); // Use internal 0.6V reference

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

float PowerManager::readBatteryVoltage()
{
    // Read battery voltage from ADC
    // XIAO nRF52840: Battery voltage is divided by 2 on P0.31
    int adcValue = analogRead(BATTERY_ADC_PIN);

    // Convert ADC value to voltage
    // nRF52840: 12-bit ADC with 0.6V internal reference
    // With gain = 1/6, can measure up to 3.6V
    float voltage = (adcValue / 4095.0) * PowerConstants::ADC_VREF * PowerConstants::BATTERY_DIVIDER;

    return voltage;
}

uint8_t PowerManager::readBatteryLevel()
{
    float voltage = readBatteryVoltage();

    // Convert voltage to percentage (assuming Li-Po battery)
    // 4.2V = 100%, 3.0V = 0%
    uint8_t level;
    if (voltage >= 4.2)
    {
        level = 100;
    }
    else if (voltage <= 3.0)
    {
        level = 0;
    }
    else
    {
        level = (uint8_t)((voltage - 3.0) / (4.2 - 3.0) * 100.0);
    }

    lastBatteryLevel = level;
    return level;
}

void PowerManager::enterLowPowerMode()
{
    Serial.println("Entering System OFF mode...");

#ifdef ARDUINO_ARCH_NRF52
    // Configure LoRa DIO0 as wake-up source (wake on HIGH)
    nrf_gpio_cfg_sense_input(LORA_DIO0, NRF_GPIO_PIN_PULLDOWN, NRF_GPIO_PIN_SENSE_HIGH);
    Serial.printf("Wake source: LoRa DIO0 (GPIO %d) - wake on HIGH\n", LORA_DIO0);

    // Configure wake button as wake-up source (wake on LOW)
    nrf_gpio_cfg_sense_input(WAKE_BUTTON, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
    Serial.printf("Wake source: Wake button (GPIO %d) - wake on LOW\n", WAKE_BUTTON);

    // Flush serial output
    Serial.flush();
    delay(100); // Allow time for serial transmission

    // Enter System OFF mode (lowest power state)
    // Current: ~0.002mA with RAM retention
    // Device will reset on wake (execution starts from setup())
    sd_power_system_off();

    // This line should never be reached
    Serial.println("ERROR: Failed to enter System OFF mode");
#else
    Serial.println("System OFF mode only available on nRF52");
#endif
}
