#ifndef ISYSTEM_PORT_H
#define ISYSTEM_PORT_H

/**
 * @brief System Utilities Port Interface (Hexagonal Architecture)
 *
 * Abstracts system-level operations:
 * - ESP32: Watchdog, LEDManager class
 * - nRF52: No watchdog, direct GPIO for LED
 */
class ISystemPort
{
public:
    virtual ~ISystemPort() = default;

    /**
     * @brief Reset/feed the watchdog timer
     * No-op if watchdog not available on platform
     */
    virtual void watchdogReset() = 0;

    /**
     * @brief Turn LED on
     */
    virtual void ledOn() = 0;

    /**
     * @brief Turn LED off
     */
    virtual void ledOff() = 0;

    /**
     * @brief Blink LED a specified number of times
     * @param count Number of blinks
     */
    virtual void ledBlink(int count) = 0;

    /**
     * @brief Delay for specified milliseconds
     * @param ms Milliseconds to delay
     */
    virtual void delay(unsigned long ms) = 0;

    /**
     * @brief Get milliseconds since boot
     * @return Milliseconds since boot
     */
    virtual unsigned long millis() = 0;
};

#endif // ISYSTEM_PORT_H
