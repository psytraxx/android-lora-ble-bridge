#include "common/LoopWake.h"
#include <Arduino.h>

// Platform-specific FreeRTOS includes
#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#elif defined(ARDUINO_ARCH_NRF52)
#include <FreeRTOS.h>
#include <semphr.h>
#endif

namespace
{
    // Binary semaphore: multiple signals before wait() collapse into one wake-up
    SemaphoreHandle_t wakeSemaphore = xSemaphoreCreateBinary();
}

namespace LoopWake
{
    void signal()
    {
        if (wakeSemaphore != nullptr)
        {
            xSemaphoreGive(wakeSemaphore);
        }
    }

    void LOOP_WAKE_ISR_ATTR signalFromISR()
    {
        if (wakeSemaphore != nullptr)
        {
            BaseType_t higherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(wakeSemaphore, &higherPriorityTaskWoken);
            portYIELD_FROM_ISR(higherPriorityTaskWoken);
        }
    }

    void wait(uint32_t timeoutMs)
    {
        if (wakeSemaphore == nullptr)
        {
            delay(timeoutMs);
            return;
        }
        xSemaphoreTake(wakeSemaphore, pdMS_TO_TICKS(timeoutMs));
    }
}
