#ifndef ESP32_SYSTEM_ADAPTER_H
#define ESP32_SYSTEM_ADAPTER_H

#include "ports/ISystemPort.h"
#include <Arduino.h>
#include <Adafruit_SleepyDog.h>
#include "esp32/LEDManager.h"
#include "esp32/FirmwareConfig.h"

/**
 * @brief ESP32 System Adapter (Watchdog, LED, Timing)
 *
 * Wraps ESP32-specific system operations
 */
class ESP32SystemAdapter : public ISystemPort
{
public:
    ESP32SystemAdapter()
#ifdef LED_PIN
        : ledManager(LED_PIN)
#endif
    {
#ifdef LED_PIN
        ledManager.setup();
#endif
    }

    void watchdogReset() override
    {
        Watchdog.reset();
    }

    void ledOn() override
    {
#ifdef LED_PIN
        ledManager.setOn();
#endif
    }

    void ledOff() override
    {
#ifdef LED_PIN
        ledManager.setOff();
#endif
    }

    void ledBlink(int count) override
    {
#ifdef LED_PIN
        ledManager.blink(count);
#endif
    }

    void delay(unsigned long ms) override
    {
        ::delay(ms);
    }

    unsigned long millis() override
    {
        return ::millis();
    }

private:
#ifdef LED_PIN
    LEDManager ledManager;
#endif
};

#endif // ESP32_SYSTEM_ADAPTER_H
