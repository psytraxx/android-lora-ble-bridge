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
     * @brief Blinks the LED for a specified duration.
     * @param duration The duration of the blink in milliseconds.
     */
    void blink(int duration = 100)
    {
        setOn();
        delay(duration);
        setOff();
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

    /**
     * @brief Blinks the LED multiple times.
     * @param times Number of blinks.
     * @param duration Duration of each blink in milliseconds.
     * @param delayBetween Delay between blinks in milliseconds.
     */
    void blinkMultiple(int times, int duration = 100, int delayBetween = 100)
    {
        for (int i = 0; i < times; i++)
        {
            blink(duration);
            if (i < times - 1)
            {
                delay(delayBetween);
            }
        }
    }

private:
    int ledPin;
};

#endif // LED_MANAGER_H