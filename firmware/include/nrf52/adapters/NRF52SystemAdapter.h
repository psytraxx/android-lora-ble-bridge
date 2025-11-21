#ifndef NRF52_SYSTEM_ADAPTER_H
#define NRF52_SYSTEM_ADAPTER_H

#include "ports/ISystemPort.h"
#include <Arduino.h>
#include "nrf52/FirmwareConfig.h"

/**
 * @brief nRF52 System Adapter (No Watchdog, Direct GPIO LED)
 *
 * Wraps nRF52-specific system operations
 */
class NRF52SystemAdapter : public ISystemPort
{
public:
    NRF52SystemAdapter()
    {
        pinMode(LED_PIN, OUTPUT);
        digitalWrite(LED_PIN, LOW);
    }

    void watchdogReset() override
    {
        // nRF52 does not use watchdog in this firmware
    }

    void ledOn() override
    {
        digitalWrite(LED_PIN, HIGH);
    }

    void ledOff() override
    {
        digitalWrite(LED_PIN, LOW);
    }

    void ledBlink(int count) override
    {
        for (int i = 0; i < count; i++)
        {
            digitalWrite(LED_PIN, HIGH);
            ::delay(LEDConstants::BLINK_DURATION_MS);
            digitalWrite(LED_PIN, LOW);
            if (i < count - 1)
            {
                ::delay(LEDConstants::BLINK_DELAY_MS);
            }
        }
    }

    void delay(unsigned long ms) override
    {
        ::delay(ms);
    }

    unsigned long millis() override
    {
        return ::millis();
    }
};

#endif // NRF52_SYSTEM_ADAPTER_H
