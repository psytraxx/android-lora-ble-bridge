#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include "common/LoopWake.h"

/**
 * @file ButtonManager.h
 * @brief Interrupt-driven helper for a single active-LOW user button.
 *
 * The button pin is configured with an internal pull-up and a falling-edge
 * interrupt. The ISR only sets a flag and wakes the main loop, so the press is
 * handled from loop() via wasPressed() without polling. Presses closer together
 * than the debounce window are ignored (contact bounce).
 */

class ButtonManager
{
public:
    /**
     * @brief Construct a new ButtonManager
     * @param pin Button GPIO pin (active LOW, internal pull-up)
     * @param debounceMs Minimum time between accepted presses
     */
    explicit ButtonManager(int pin, unsigned long debounceMs = 200)
        : buttonPin(pin), debounceInterval(debounceMs) {}

    /**
     * @brief Configure the button GPIO and attach the falling-edge interrupt.
     */
    void setup()
    {
        if (buttonPin < 0)
            return;
        instance = this;
        pinMode(buttonPin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(buttonPin), isrTrampoline, FALLING);
    }

    /**
     * @brief Consume a pending press, if any (call from loop()).
     *
     * @return true exactly once per debounced press
     */
    bool wasPressed()
    {
        if (!pressedFlag)
            return false;
        pressedFlag = false;

        // Debounce in task context: the ISR timestamp decides, so a bouncing
        // contact cannot queue up several presses.
        unsigned long now = millis();
        if (now - lastPressTime < debounceInterval)
            return false;
        lastPressTime = now;
        return true;
    }

private:
    // Defined in ButtonManager.cpp: on Xtensa an IRAM ISR must live in a
    // translation unit of its own so its literal pool is placed correctly.
    static void LOOP_WAKE_ISR_ATTR isrTrampoline();

    static ButtonManager *instance;

    const int buttonPin;
    const unsigned long debounceInterval;
    volatile bool pressedFlag = false;
    unsigned long lastPressTime = 0;
};

#endif // BUTTON_MANAGER_H
