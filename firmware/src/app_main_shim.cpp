// app_main_shim.cpp
// Provide app_main for ESP-IDF builds so Arduino-style setup()/loop() run.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Prefer Arduino's configured loop stack size if available.
#ifndef ARDUINO_LOOP_STACK_SIZE
#define ARDUINO_LOOP_STACK_SIZE 8192
#endif

static void arduinoLoopTask(void *parameter)
{
    (void)parameter;

    // Call Arduino setup once
    setup();

    // Run the Arduino loop continuously
    for (;;)
    {
        loop();
        if (serialEventRun)
        {
            serialEventRun();
        }
        vTaskDelay(1);
    }
}

extern "C" void app_main(void)
{
    initArduino();

    // Run Arduino loop on the configured core (ESP-IDF + Arduino hybrid)
    const BaseType_t ok = xTaskCreatePinnedToCore(
        arduinoLoopTask,
        "arduino-loop",
        ARDUINO_LOOP_STACK_SIZE,
        nullptr,
        1,
        nullptr,
        ARDUINO_RUNNING_CORE);

    if (ok != pdPASS)
    {
        // If task creation fails, block here to avoid running without loop.
        for (;;)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
}
