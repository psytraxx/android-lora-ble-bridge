#include <esp32-hal.h>
#ifndef LED_MANAGER_H
#define LED_MANAGER_H

class LEDManager
{
public:
    LEDManager(int pin) : ledPin(pin) {}

    /**
     * @brief Initializes the LED.
     */
    void setup()
    {
        pinMode(ledPin, OUTPUT);
        digitalWrite(ledPin, LOW); // Ensure LED is off initially
    }

    /**
     * @brief Blinks the LED a specified number of times.
     * @param times Number of blinks (default: 1).
     * @param duration Duration of each blink in milliseconds (default: 50 - reduced for power saving).
     * @param delayBetween Delay between blinks in milliseconds (default: 200 - increased for power saving).
     */
    void blink(int times = 1, int duration = 50, int delayBetween = 200)
    {
        for (int i = 0; i < times; i++)
        {
            setOn();
            delay(duration);
            setOff();
            if (i < times - 1)
            {
                delay(delayBetween);
            }
        }
    }

    /**
     * @brief Turns the LED on.
     */
    void setOn()
    {
        digitalWrite(ledPin, HIGH);
    }

    /**
     * @brief Turns the LED off.
     */
    void setOff()
    {
        digitalWrite(ledPin, LOW);
    }

private:
    int ledPin;
};

#endif // LED_MANAGER_H