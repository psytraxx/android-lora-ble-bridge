#include "BleTask.h"
#include "LEDManager.h"
#include "esp_log.h"

// External LED manager reference (if defined)
#ifdef LED_PIN
extern LEDManager ledManager;
#endif

static const char *TAG = "BleTask";

namespace BleTask
{
    // Task configuration
    static constexpr int TASK_PRIORITY = 3;
    static constexpr int TASK_STACK_SIZE = 4096;
    static constexpr TickType_t TASK_TIMEOUT = pdMS_TO_TICKS(100);

    // Task handle (global for notifications)
    static TaskHandle_t taskHandle = nullptr;

    // Task context
    static TaskContext *context = nullptr;

    /**
     * @brief Process LoRa → BLE message queue
     */
    static void processLoRaToBleQueue()
    {
        Message loraMsg;
        while (xQueueReceive(context->loraToBleQueue, &loraMsg, 0) == pdTRUE)
        {
            ESP_LOGI(TAG, "LoRa → BLE, type=%d", (int)loraMsg.type);

            // Check if connected (via state machine)
            if (context->appController->isConnected())
            {
                // Check if Android has enabled notifications (proper way!)
                if (context->bleManager->areNotificationsEnabled())
                {
                    // Send directly via BLE
                    if (context->bleManager->sendMessage(loraMsg))
                    {
                        ESP_LOGI(TAG, "Message forwarded to BLE");
#ifdef LED_PIN
                        ledManager.blink(LEDConstants::RX_BLINKS);
#endif
                    }
                    else
                    {
                        // Send failed, buffer it
                        context->messageBuffer->add(loraMsg);
                        ESP_LOGW(TAG, "BLE send failed, buffered message");
                    }
                }
                else
                {
                    // Android hasn't enabled notifications yet, buffer message
                    context->messageBuffer->add(loraMsg);
                    ESP_LOGI(TAG, "Notifications not enabled yet, buffered message");
                }
            }
            else
            {
                // BLE disconnected, buffer message
                context->messageBuffer->add(loraMsg);
                ESP_LOGI(TAG, "BLE disconnected, buffered (total: %d)", context->messageBuffer->getCount());
            }
        }
    }

    /**
     * @brief Forward buffered messages to BLE when Android ready
     */
    static void forwardBufferedMessages()
    {
        // Only forward if connected and notifications enabled
        if (!context->appController->isConnected() ||
            !context->bleManager->areNotificationsEnabled() ||
            context->messageBuffer->isEmpty())
        {
            return;
        }

        ESP_LOGI(TAG, "Forwarding %d buffered messages", context->messageBuffer->getCount());

        // Drain buffer
        Message bufferedMsg;
        while (context->messageBuffer->peek(bufferedMsg))
        {
            if (context->bleManager->sendMessage(bufferedMsg))
            {
                // Message sent successfully
                context->messageBuffer->popFront();
                ESP_LOGI(TAG, "Buffered message sent");
#ifdef LED_PIN
                ledManager.blink(LEDConstants::RX_BLINKS);
#endif
                // Spacing between messages to avoid overwhelming BLE stack
                vTaskDelay(pdMS_TO_TICKS(BLEConstants::MESSAGE_SPACING_MS));
            }
            else
            {
                ESP_LOGW(TAG, "Failed to send buffered message");
                break; // Stop trying, keep message in buffer
            }
        }
    }

    /**
     * @brief BLE task main function
     */
    static void taskFunction(void *param)
    {
        context = static_cast<TaskContext *>(param);
        ESP_LOGI(TAG, "BLE Task started (Priority: %d, Stack: %d bytes)", TASK_PRIORITY, TASK_STACK_SIZE);

        while (1)
        {
            // Wait for notification or timeout
            ulTaskNotifyTake(pdTRUE, TASK_TIMEOUT);

            // Process LoRa → BLE message queue
            processLoRaToBleQueue();

            // Try to forward buffered messages (if Android ready)
            forwardBufferedMessages();
        }
    }

    // ========================================================================
    // Public API
    // ========================================================================

    TaskHandle_t start(
        ApplicationController *appCtrl,
        BLEManager *bleMgr,
        MessageBuffer *msgBuffer,
        QueueHandle_t loraToBleQ)
    {
        // Allocate task context (never freed - lives for entire program lifetime)
        TaskContext *ctx = new TaskContext{
            .appController = appCtrl,
            .bleManager = bleMgr,
            .messageBuffer = msgBuffer,
            .loraToBleQueue = loraToBleQ};

        // Create task
        BaseType_t result = xTaskCreate(
            taskFunction,
            "BleTask",
            TASK_STACK_SIZE,
            ctx,
            TASK_PRIORITY,
            &taskHandle);

        if (result != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create BLE task!");
            delete ctx;
            return nullptr;
        }

        ESP_LOGI(TAG, "BLE task created successfully");
        return taskHandle;
    }

    TaskHandle_t getHandle()
    {
        return taskHandle;
    }

    void notifyMessageReceived()
    {
        if (taskHandle != nullptr)
        {
            xTaskNotifyGive(taskHandle);
        }
    }

    void notifyConnectionChange(bool connected)
    {
        if (taskHandle != nullptr)
        {
            ESP_LOGI(TAG, "BLE connection change: %s", connected ? "connected" : "disconnected");

            // Notify task to process buffered messages or handle disconnection
            xTaskNotifyGive(taskHandle);
        }
    }

} // namespace BleTask
