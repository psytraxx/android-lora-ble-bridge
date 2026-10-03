#ifndef LOOP_WAKE_H
#define LOOP_WAKE_H

#include <stdint.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_attr.h>
#define LOOP_WAKE_ISR_ATTR IRAM_ATTR
#elif defined(ARDUINO_ARCH_NRF52)
#define LOOP_WAKE_ISR_ATTR
#else
#error "Unsupported platform"
#endif

/**
 * @brief Event-driven main loop wake-up
 *
 * loop() blocks in wait() instead of polling with delay(). While blocked, the
 * FreeRTOS idle task lets the MCU sleep (nRF52: sd_app_evt_wait, ESP32: tickless
 * auto light sleep). Any event that needs main-loop work calls signal() so the
 * loop runs immediately instead of after the timeout.
 */
namespace LoopWake
{
    /// Wake the main loop (task context)
    void signal();

    /// Wake the main loop (interrupt context)
    void LOOP_WAKE_ISR_ATTR signalFromISR();

    /// Block until signalled or timeoutMs elapses
    void wait(uint32_t timeoutMs);
}

#endif // LOOP_WAKE_H
