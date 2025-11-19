#ifndef POWER_TASK_H
#define POWER_TASK_H

#include "ApplicationController.h"
#include "BLEManager.h"
#include "LoRaManager.h"
#include "FirmwareConfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @file PowerTask.h
 * @brief FreeRTOS task for power management and timeout monitoring
 *
 * Responsibilities:
 *  - Monitor advertising timeout (30s → deep sleep)
 *  - Monitor inactivity timeout (60s → disconnect)
 *  - Trigger deep sleep when appropriate
 *  - Trigger BLE disconnection on inactivity
 *
 * Priority: 2 (low - not time-critical, runs every 1 second)
 * Stack Size: 2048 bytes
 *
 * Wakes on:
 *  - Periodic timeout (1 second) for checking timeouts
 */

namespace PowerTask
{
    /**
     * @brief Task context structure passed to Power task
     */
    struct TaskContext
    {
        ApplicationController *appController;
        BLEManager *bleManager;
        LoRaManager *loraManager;
    };

    /**
     * @brief Create and start Power task
     * @param appCtrl Application controller (state machine)
     * @param bleMgr BLE manager (for disconnect and advertising control)
     * @param loraMgr LoRa manager (for RX mode control before sleep)
     * @return Task handle, or nullptr on failure
     */
    TaskHandle_t start(
        ApplicationController *appCtrl,
        BLEManager *bleMgr,
        LoRaManager *loraMgr);

    /**
     * @brief Get Power task handle
     * @return Task handle, or nullptr if not started
     */
    TaskHandle_t getHandle();

} // namespace PowerTask

#endif // POWER_TASK_H
