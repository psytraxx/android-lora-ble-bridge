#ifndef BLE_TASK_H
#define BLE_TASK_H

#include "ApplicationController.h"
#include "BLEManager.h"
#include "MessageBuffer.h"
#include "FirmwareConfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

/**
 * @file BleTask.h
 * @brief FreeRTOS task for BLE message processing
 *
 * Responsibilities:
 *  - Process LoRa → BLE message queue
 *  - Forward buffered messages when Android ready
 *  - Handle BLE connection state changes
 *  - Notify ApplicationController of connection events
 *
 * Priority: 3 (medium - important but not time-critical)
 * Stack Size: 4096 bytes
 *
 * Wakes on:
 *  - Task notification from LoRa task (message received)
 *  - Task notification from BLE connection callback
 *  - Periodic timeout (100ms) for buffered message forwarding
 */

namespace BleTask
{
    /**
     * @brief Task context structure passed to BLE task
     */
    struct TaskContext
    {
        ApplicationController *appController;
        BLEManager *bleManager;
        MessageBuffer *messageBuffer;
        QueueHandle_t loraToBleQueue;
    };

    /**
     * @brief Create and start BLE task
     * @param appCtrl Application controller (state machine)
     * @param bleMgr BLE manager
     * @param msgBuffer Message buffer for offline storage
     * @param loraToBleQ Queue for LoRa → BLE messages
     * @return Task handle, or nullptr on failure
     */
    TaskHandle_t start(
        ApplicationController *appCtrl,
        BLEManager *bleMgr,
        MessageBuffer *msgBuffer,
        QueueHandle_t loraToBleQ);

    /**
     * @brief Get BLE task handle for notifications
     * @return Task handle, or nullptr if not started
     */
    TaskHandle_t getHandle();

    /**
     * @brief Notify BLE task of new message in queue
     * Safe to call from ISR context
     */
    void notifyMessageReceived();

    /**
     * @brief Notify BLE task of connection state change
     * @param connected true if connected, false if disconnected
     */
    void notifyConnectionChange(bool connected);

} // namespace BleTask

#endif // BLE_TASK_H
