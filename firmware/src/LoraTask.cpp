#include "LoraTask.h"
#include "BleTask.h"
#include "LEDManager.h"
#include "Protocol.h"
#include <esp_task_wdt.h>
#include "esp_log.h"

// External LED manager reference (if defined)
#ifdef LED_PIN
extern LEDManager ledManager;
#endif

static const char *TAG = "LoraTask";

namespace LoraTask
{
    // Task configuration
    static constexpr int TASK_PRIORITY = 4; // Highest priority
    static constexpr int TASK_STACK_SIZE = 4096;
    static constexpr TickType_t TASK_TIMEOUT = pdMS_TO_TICKS(50);

    // Task handle (global for notifications)
    static TaskHandle_t taskHandle = nullptr;

    // Task context
    static TaskContext *context = nullptr;

    /**
     * @brief Queue message to LoRa→BLE queue (with overflow to NVS buffer)
     */
    static void queueOrBufferMessage(const Message &msg, const char *msgTypeName)
    {
        if (xQueueSend(context->loraToBleQueue, &msg, 0) != pdTRUE)
        {
            // Queue is full - buffer to NVS instead of dropping
            ESP_LOGW(TAG, "LoRa to BLE queue full, buffering %s to NVS", msgTypeName);
            context->messageBuffer->add(msg);
        }
        else
        {
            // Notify BLE task that a message is ready
            BleTask::notifyMessageReceived();
        }
    }

    /**
     * @brief Handle received LoRa packet
     */
    static void handlePacketReceived(const LoRaPacket &packet)
    {
        ESP_LOGI(TAG, "Packet received: len=%d, rssi=%d, snr=%.2f",
                 packet.len, packet.rssi, packet.snr);

        // Notify application controller of activity
        context->appController->notifyActivity();

        // Deserialize message
        Message msg;
        if (!msg.deserialize(packet.buffer, packet.len))
        {
            ESP_LOGE(TAG, "Failed to deserialize LoRa message");
            return;
        }

        ESP_LOGI(TAG, "Deserialized: type=%d", (int)msg.type);

        // Handle message types
        switch (msg.type)
        {
        case MessageType::Text:
        {
            const auto& text = msg.textData();
            ESP_LOGI(TAG, "Text - seq: %d, text: \"%s\"", text.seq, text.text);

            if (text.hasGps)
            {
                ESP_LOGI(TAG, "GPS: %f°, %f°", text.lat / 1000000.0, text.lon / 1000000.0);
            }

            // Send ACK
            auto ack = Message::createAck(text.seq);
            uint8_t ackBuf[64];
            auto ackLen = ack.serialize(ackBuf, sizeof(ackBuf));

            if (ackLen > 0)
            {
                ESP_LOGI(TAG, "Sending ACK for seq: %d", text.seq);

                // Wait before sending ACK to ensure sender has switched to RX mode
                vTaskDelay(pdMS_TO_TICKS(LoRaConstants::ACK_DELAY_MS));

                // Reset watchdog before long LoRa transmission
                esp_task_wdt_reset();

                // Start non-blocking transmission
                context->loraManager->startTransmit(ackBuf, ackLen);
            }

            // Queue or buffer message for BLE delivery
            queueOrBufferMessage(msg, "text message");

#ifdef LED_PIN
            ledManager.blink();
#endif
            break;
        }

        case MessageType::Ack:
        {
            const auto& ack = msg.ackData();
            ESP_LOGI(TAG, "ACK - seq: %d", ack.seq);

            // Queue or buffer ACK for BLE delivery
            queueOrBufferMessage(msg, "ACK");

#ifdef LED_PIN
            ledManager.blink();
#endif
            break;
        }

        case MessageType::WakeUp:
        {
            ESP_LOGI(TAG, "WakeUp message received");
            // Wake-up messages don't need to be forwarded to BLE
            break;
        }

        default:
        {
            ESP_LOGW(TAG, "Unknown message type received");
            break;
        }
        }
    }

    /**
     * @brief Process BLE → LoRa message queue
     */
    static void processBleToLoraQueue()
    {
        Message bleMsg;
        while (xQueueReceive(context->bleToLoraQueue, &bleMsg, 0) == pdTRUE)
        {
            ESP_LOGI(TAG, "BLE → LoRa, type=%d", (int)bleMsg.type);

            // Serialize and transmit via LoRa
            uint8_t buf[BufferConstants::MAX_PROTOCOL_MESSAGE];
            int len = bleMsg.serialize(buf, sizeof(buf));

            if (len > 0)
            {
                // Reset watchdog before long LoRa transmission
                esp_task_wdt_reset();

                // Start non-blocking transmission via LoRaManager
                if (context->loraManager->startTransmit(buf, len))
                {
#ifdef LED_PIN
                    ledManager.blink(LEDConstants::TX_BLINKS);
#endif
                }
            }
            else
            {
                ESP_LOGE(TAG, "Failed to serialize message for LoRa TX");
            }

            // Update activity
            context->appController->notifyActivity();
        }
    }

    /**
     * @brief LoRa task main function
     */
    static void taskFunction(void *param)
    {
        context = static_cast<TaskContext *>(param);
        ESP_LOGI(TAG, "LoRa Task started (Priority: %d, Stack: %d bytes)", TASK_PRIORITY, TASK_STACK_SIZE);

        // Register task with watchdog
        esp_task_wdt_add(xTaskGetCurrentTaskHandle());

        // Set LoRa callbacks to notify this task
        context->loraManager->setReceiveCallback([](const LoRaPacket &packet)
                                                  {
            // This runs in ISR context or callback context
            // Just forward to task for processing
            handlePacketReceived(packet); });

        context->loraManager->setTransmitCallback([](bool success)
                                                   {
            if (success) {
                ESP_LOGI(TAG, "LoRa transmission completed successfully");
            } else {
                ESP_LOGW(TAG, "LoRa transmission failed");
            } });

        while (1)
        {
            // Reset watchdog
            esp_task_wdt_reset();

            // Wait for notification or timeout
            ulTaskNotifyTake(pdTRUE, TASK_TIMEOUT);

            // Process LoRa hardware events (RX/TX completion)
            context->loraManager->process();

            // Process BLE → LoRa message queue
            processBleToLoraQueue();
        }
    }

    // ========================================================================
    // Public API
    // ========================================================================

    TaskHandle_t start(
        ApplicationController *appCtrl,
        LoRaManager *loraMgr,
        MessageBuffer *msgBuffer,
        QueueHandle_t bleToLoraQ,
        QueueHandle_t loraToBleQ)
    {
        // Allocate task context (never freed - lives for entire program lifetime)
        TaskContext *ctx = new TaskContext{
            .appController = appCtrl,
            .loraManager = loraMgr,
            .messageBuffer = msgBuffer,
            .bleToLoraQueue = bleToLoraQ,
            .loraToBleQueue = loraToBleQ};

        // Create task
        BaseType_t result = xTaskCreate(
            taskFunction,
            "LoraTask",
            TASK_STACK_SIZE,
            ctx,
            TASK_PRIORITY,
            &taskHandle);

        if (result != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create LoRa task!");
            delete ctx;
            return nullptr;
        }

        ESP_LOGI(TAG, "LoRa task created successfully");
        return taskHandle;
    }

    TaskHandle_t getHandle()
    {
        return taskHandle;
    }

    void notifyMessageQueued()
    {
        if (taskHandle != nullptr)
        {
            xTaskNotifyGive(taskHandle);
        }
    }

    void notifyPacketReceived()
    {
        if (taskHandle != nullptr)
        {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            vTaskNotifyGiveFromISR(taskHandle, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }

    void notifyTransmitComplete()
    {
        if (taskHandle != nullptr)
        {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            vTaskNotifyGiveFromISR(taskHandle, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }

} // namespace LoraTask
