// app_main_shim.cpp
// Provide app_main for ESP-IDF builds so Arduino-style setup()/loop() run.

#include <Arduino.h>

extern "C" void app_main(void)
{
    initArduino();

    // Call Arduino setup once
    setup();

    // Run the Arduino loop continuously
    while (true)
    {
        loop();
        yield();
        delay(1);
    }
}
