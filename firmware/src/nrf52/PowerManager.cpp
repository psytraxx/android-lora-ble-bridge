#include "nrf52/PowerManager.h"
#include "nrf52/FirmwareConfig.h"

PowerManager::PowerManager()
    : lastBatteryLevel(100)
{
}

bool PowerManager::begin()
{
    // Configure ADC for battery monitoring
    analogReadResolution(PowerConstants::ADC_RESOLUTION_BITS);
    analogReference(AR_INTERNAL); // Use internal 0.6V reference

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
    // TODO: Implement nRF52 low-power mode
    // - Configure wake sources (LoRa DIO0, button)
    // - Enter System OFF mode
    Serial.println("Low-power mode not yet implemented");
}
