#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>

/**
 * @file LEDManager.h
 * @brief Simple helper to control a status LED connected to a single GPIO.
 *
 * The LEDManager is intentionally minimal: it provides setup, on/off and a
 * convenience blink method used for user-visible status indications. The
 * implementation uses Arduino GPIO APIs.
 */

class LEDManager
{
public:
    /**
     * @brief Construct a new LEDManager
     * @param pin GPIO pin number the LED is connected to
     * @param hbIntervalMs Heartbeat interval in milliseconds (default: 1000)
     * @param hbDurationMs Heartbeat blink duration in milliseconds (default: 50)
     */
    explicit LEDManager(int pin, unsigned long hbIntervalMs = 1000, int hbDurationMs = 50)
        : ledPin(pin), blinkActive(false), blinkCount(0),
          blinkTarget(0), blinkDuration(50), blinkDelay(200),
          lastStateChange(0), ledState(false),
          lastHeartbeat(0), heartbeatIntervalMs(hbIntervalMs), heartbeatDurationMs(hbDurationMs) {}

    /**
     * @brief Initialize the LED GPIO. Sets the pin mode and ensures LED is off.
     */
    void setup()
    {
        pinMode(ledPin, OUTPUT);
        digitalWrite(ledPin, LOW); // Ensure LED is off initially
    }

    /**
     * @brief Start a non-blocking blink sequence.
     *
     * @param times Number of blinks
     * @param duration Time LED stays on in each blink in milliseconds (default: 50)
     * @param delayBetween Delay between blinks in milliseconds (default: 200)
     */
    void blink(int times = 1, int duration = 50, int delayBetween = 200)
    {
        blinkActive = true;
        blinkTarget = times;
        blinkCount = 0;
        blinkDuration = duration;
        blinkDelay = delayBetween;
        ledState = false;
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
                blink(1, heartbeatDurationMs, 0);
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
    bool blinkActive;
    int blinkCount;
    int blinkTarget;
    int blinkDuration;
    int blinkDelay;
    unsigned long lastStateChange;
    bool ledState;
    unsigned long lastHeartbeat;
    unsigned long heartbeatIntervalMs;
    int heartbeatDurationMs;
};

#endif // LED_MANAGER_H