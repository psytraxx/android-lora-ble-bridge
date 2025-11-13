#ifndef LORA_TASK_H
#define LORA_TASK_H

#include "ApplicationController.h"
#include "LoRaManager.h"
#include "MessageBuffer.h"
#include "FirmwareConfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

/**
 * @file LoraTask.h
 * @brief FreeRTOS task for LoRa message processing
 *
 * Responsibilities:
 *  - Process BLE → LoRa message queue (TX)
 *  - Process LoRa RX events (from ISR)
 *  - Send ACKs for received messages
 *  - Forward received messages to BLE task
 *  - Notify ApplicationController of activity
 *
 * Priority: 4 (highest - time-critical radio operations)
 * Stack Size: 4096 bytes
 *
 * Wakes on:
 *  - Task notification from LoRa ISR (packet RX/TX complete)
 *  - Task notification when message added to bleToLoraQueue
 *  - Periodic timeout (50ms) for processing LoRa hardware events
 */

namespace LoraTask
{
    /**
     * @brief Task context structure passed to LoRa task
     */
    struct TaskContext
    {
        ApplicationController *appController;
        LoRaManager *loraManager;
        MessageBuffer *messageBuffer;
        QueueHandle_t bleToLoraQueue;
        QueueHandle_t loraToBleQueue;
    };

    /**
     * @brief Create and start LoRa task
     * @param appCtrl Application controller (state machine)
     * @param loraMgr LoRa manager
     * @param msgBuffer Message buffer for queue overflow
     * @param bleToLoraQ Queue for BLE → LoRa messages
     * @param loraToBleQ Queue for LoRa → BLE messages
     * @return Task handle, or nullptr on failure
     */
    TaskHandle_t start(
        ApplicationController *appCtrl,
        LoRaManager *loraMgr,
        MessageBuffer *msgBuffer,
        QueueHandle_t bleToLoraQ,
        QueueHandle_t loraToBleQ);

    /**
     * @brief Get LoRa task handle for notifications
     * @return Task handle, or nullptr if not started
     */
    TaskHandle_t getHandle();

    /**
     * @brief Notify LoRa task of new message in queue
     * Safe to call from any context
     */
    void notifyMessageQueued();

    /**
     * @brief Notify LoRa task of packet received (from ISR)
     * Safe to call from ISR context
     */
    void notifyPacketReceived();

    /**
     * @brief Notify LoRa task of transmit complete (from ISR)
     * Safe to call from ISR context
     */
    void notifyTransmitComplete();

} // namespace LoraTask

#endif // LORA_TASK_H
