#include <esp32-hal.h>
#ifndef LED_MANAGER_H
#define LED_MANAGER_H

/**
 * @file LEDManager.h
 * @brief Simple helper to control a status LED connected to a single GPIO.
 *
 * The LEDManager is intentionally minimal: it provides setup, on/off and a
 * convenience blink method used for user-visible status indications. The
 * implementation uses Arduino-style APIs (pinMode, digitalWrite) for portability
 * across ESP32 Arduino-based builds.
 */

class LEDManager
{
public:
    /**
     * @brief Construct a new LEDManager
     * @param pin GPIO pin number the LED is connected to
     */
    explicit LEDManager(int pin) : ledPin(pin) {}

    /**
     * @brief Initialize the LED GPIO. Sets the pin mode and ensures LED is off.
     */
    void setup()
    {
        pinMode(ledPin, OUTPUT);
        digitalWrite(ledPin, LOW); // Ensure LED is off initially
    }

    /**
     * @brief Blink the LED a number of times.
     *
     * This is a blocking convenience method intended for simple status
     * indications during setup or error reporting. For non-blocking patterns
     * use the setOn/setOff methods and an application-level timer.
     *
     * @param times Number of blinks (default: 1)
     * @param duration Time LED stays on in each blink in milliseconds (default: 50)
     * @param delayBetween Delay between blinks in milliseconds (default: 200)
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
     * @brief Turn the LED on (drive pin HIGH).
     */
    void setOn()
    {
        digitalWrite(ledPin, HIGH);
    }

    /**
     * @brief Turn the LED off (drive pin LOW).
     */
    void setOff()
    {
        digitalWrite(ledPin, LOW);
    }

private:
    int ledPin;
};

#endif // LED_MANAGER_H