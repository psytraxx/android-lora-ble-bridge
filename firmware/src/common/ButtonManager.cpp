#include "common/ButtonManager.h"

// Single-button boards: the ISR has no user argument on all supported
// platforms, so the trampoline dispatches through this instance pointer.
ButtonManager *ButtonManager::instance = nullptr;

void LOOP_WAKE_ISR_ATTR ButtonManager::isrTrampoline()
{
    if (instance != nullptr)
    {
        instance->pressedFlag = true;
    }
    LoopWake::signalFromISR();
}
