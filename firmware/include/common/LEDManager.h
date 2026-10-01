#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>

/**
 * @file LEDManager.h
 * @brief Simple helper to control a single-color or RGB status LED.
 *
 * The LEDManager is intentionally minimal: it provides setup, on/off and a
 * convenience blink method used for user-visible status indications. Boards
 * with an RGB LED pass red/blue pins; requesting a color whose pin is absent
 * falls back to green (the primary LED). The implementation uses Arduino GPIO APIs.
 */

class LEDManager
{
public:
    enum class Color : uint8_t
    {
        Green,
        Red,
        Blue
    };

    /**
     * @brief Construct a new LEDManager
     * @param greenPin Primary LED pin (always required; used for heartbeat and fallback)
     * @param redPin Optional red LED pin (-1 = not available)
     * @param bluePin Optional blue LED pin (-1 = not available)
     * @param activeLow True if the LEDs light when the pin is driven LOW
     * @param hbIntervalMs Heartbeat interval in milliseconds (default: 1000)
     * @param hbDurationMs Heartbeat blink duration in milliseconds (default: 50)
     */
    explicit LEDManager(int green, int red = -1, int blue = -1, bool lowActive = false,
                        unsigned long hbIntervalMs = 1000, unsigned long hbDurationMs = 50)
        : greenPin(green), redPin(red), bluePin(blue), activeLow(lowActive),
          ledPin(green), blinkActive(false), blinkCount(0),
          blinkTarget(0), blinkDuration(50), blinkDelay(200),
          lastStateChange(0), ledState(false),
          lastHeartbeat(0), heartbeatIntervalMs(hbIntervalMs), heartbeatDurationMs(hbDurationMs) {}

    /**
     * @brief Initialize the LED GPIOs. Sets the pin modes and ensures LEDs are off.
     */
    void setup()
    {
        for (int pin : {greenPin, redPin, bluePin})
        {
            if (pin < 0)
                continue;
            pinMode(pin, OUTPUT);
            write(pin, false); // Ensure LED is off initially
        }
    }

    /**
     * @brief Start a non-blocking blink sequence.
     *
     * @param times Number of blinks
     * @param color LED color (falls back to green if that pin is unavailable)
     * @param duration Time LED stays on in each blink in milliseconds (default: 50)
     * @param delayBetween Delay between blinks in milliseconds (default: 200)
     */
    void blink(int times = 1, Color color = Color::Green, unsigned long duration = 50, unsigned long delayBetween = 200)
    {
        if (blinkActive)
            setOff(); // Don't leave a previous color lit
        ledPin = pinFor(color);
        blinkActive = true;
        blinkTarget = times;
        blinkCount = 0;
        blinkDuration = duration;
        blinkDelay = delayBetween;
        lastStateChange = millis();
        setOn(); // Start first blink
        ledState = true;
    }

    /**
     * @brief Update the LED state machine. Call this from the main loop.
     *
     * Handles both active blink sequences and periodic heartbeat when enabled.
     */
    void update()
    {
        unsigned long now = millis();

        if (blinkActive)
        {
            unsigned long elapsed = (unsigned long)(now - lastStateChange);

            if (ledState)
            {
                // LED is currently ON
                if (elapsed >= blinkDuration)
                {
                    setOff();
                    ledState = false;
                    lastStateChange = now;
                    blinkCount++;

                    if (blinkCount >= blinkTarget)
                    {
                        blinkActive = false; // Completed all blinks
                    }
                }
            }
            else
            {
                // LED is currently OFF (between blinks)
                if (elapsed >= blinkDelay && blinkCount < blinkTarget)
                {
                    setOn();
                    ledState = true;
                    lastStateChange = now;
                }
            }
        }
        else
        {
            // No active blink - check for heartbeat
            if ((now - lastHeartbeat) >= heartbeatIntervalMs)
            {
                lastHeartbeat = now;
                blink(1, Color::Green, heartbeatDurationMs, 0);
            }
        }
    }

    /**
     * @brief Turn the current LED on.
     */
    void setOn()
    {
        write(ledPin, true);
    }

    /**
     * @brief Turn the current LED off.
     */
    void setOff()
    {
        write(ledPin, false);
    }

private:
    int pinFor(Color color) const
    {
        switch (color)
        {
        case Color::Red:
            return redPin >= 0 ? redPin : greenPin;
        case Color::Blue:
            return bluePin >= 0 ? bluePin : greenPin;
        default:
            return greenPin;
        }
    }

    void write(int pin, bool on)
    {
        digitalWrite(pin, (on != activeLow) ? HIGH : LOW);
    }

    int greenPin;
    int redPin;
    int bluePin;
    bool activeLow;
    int ledPin; // Pin of the color currently blinking
    bool blinkActive;
    int blinkCount;
    int blinkTarget;
    unsigned long blinkDuration;
    unsigned long blinkDelay;
    unsigned long lastStateChange;
    bool ledState;
    unsigned long lastHeartbeat;
    unsigned long heartbeatIntervalMs;
    unsigned long heartbeatDurationMs;
};

#endif // LED_MANAGER_H
