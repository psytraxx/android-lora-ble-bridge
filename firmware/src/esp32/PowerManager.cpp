#include "esp32/PowerManager.h"
#include "esp32/FirmwareConfig.h"
#include <Arduino.h>

// Battery voltage to percentage lookup table
const float MIN_VOLTAGE = 3.0;
const float MAX_VOLTAGE = 4.2;

void PowerManager::configurePowerManagement()
{
    // Configure battery ADC pin
#ifdef BATTERY_ADC_PIN
    pinMode(BATTERY_ADC_PIN, INPUT);

#ifdef BATTERY_ADC_CTRL
    // Enable battery voltage reading (Heltec boards)
    pinMode(BATTERY_ADC_CTRL, OUTPUT);
    digitalWrite(BATTERY_ADC_CTRL, LOW); // Enable ADC reading
#endif
#endif

    Serial.println("Power management configured");
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
