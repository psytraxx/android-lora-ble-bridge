#include "PowerTask.h"
#include "PowerManager.h"
#include "esp_log.h"

static const char *TAG = "PowerTask";

namespace PowerTask
{
    // Task configuration
    static constexpr int TASK_PRIORITY = 2; // Low priority
    static constexpr int TASK_STACK_SIZE = 2048;
    static constexpr TickType_t CHECK_INTERVAL = pdMS_TO_TICKS(1000); // 1 second

    // Task handle
    static TaskHandle_t taskHandle = nullptr;

    // Task context
    static TaskContext *context = nullptr;

    /**
     * @brief Check advertising timeout and trigger deep sleep
     */
    static void checkAdvertisingTimeout()
    {
        // Only check when disconnected
        if (context->appController->getState() != AppState::DISCONNECTED_ADVERTISING)
        {
            return;
        }

        unsigned long advertisingDuration = context->appController->getAdvertisingDuration();

        if (advertisingDuration >= PowerConstants::ADVERTISE_DURATION_MS)
        {
            ESP_LOGI(TAG, "Advertising timeout (%lu ms) - entering deep sleep",
                     PowerConstants::ADVERTISE_DURATION_MS);

            // Stop advertising before sleep
            context->bleManager->stopAdvertising();

            // Enter deep sleep (does not return - device will reset on wake)
            PowerManager::enterDeepSleep();
        }
    }

    /**
     * @brief Check inactivity timeout and trigger disconnect
     */
    static void checkInactivityTimeout()
    {
        // Only check when connected
        if (context->appController->getState() != AppState::CONNECTED_ACTIVE)
        {
            return;
        }

        unsigned long inactivityDuration = context->appController->getInactivityDuration();

        if (inactivityDuration >= PowerConstants::INACTIVITY_TIMEOUT_MS)
        {
            ESP_LOGI(TAG, "Inactivity timeout (%lu ms) - forcing disconnect",
                     PowerConstants::INACTIVITY_TIMEOUT_MS);

            // Force disconnect
            context->bleManager->disconnect();

            // State will transition to DISCONNECTED_ADVERTISING automatically
            // when BLE task detects disconnection
        }
    }

    /**
     * @brief Power task main function
     */
    static void taskFunction(void *param)
    {
        context = static_cast<TaskContext *>(param);
        ESP_LOGI(TAG, "Power Task started (Priority: %d, Stack: %d bytes)", TASK_PRIORITY, TASK_STACK_SIZE);

        while (1)
        {
            // Sleep for 1 second
            vTaskDelay(CHECK_INTERVAL);

            // Check timeouts
            checkAdvertisingTimeout();
            checkInactivityTimeout();

            // Log state periodically (every 10 seconds)
            static int loopCount = 0;
            if (++loopCount >= 10)
            {
                loopCount = 0;
                AppState state = context->appController->getState();
                ESP_LOGD(TAG, "State: %s, Advertising: %lu ms, Inactivity: %lu ms, Connection: %lu ms",
                         state == AppState::CONNECTED_ACTIVE ? "CONNECTED" : "DISCONNECTED",
                         context->appController->getAdvertisingDuration(),
                         context->appController->getInactivityDuration(),
                         context->appController->getConnectionDuration());
            }

            // Update battery level periodically (every 30 seconds)
            static int batteryUpdateCount = 0;
            if (++batteryUpdateCount >= 30)
            {
                batteryUpdateCount = 0;
                context->bleManager->updateBatteryLevel();
            }
        }
    }

    // ========================================================================
    // Public API
    // ========================================================================

    TaskHandle_t start(
        ApplicationController *appCtrl,
        BLEManager *bleMgr)
    {
        // Allocate task context (never freed - lives for entire program lifetime)
        TaskContext *ctx = new TaskContext{
            .appController = appCtrl,
            .bleManager = bleMgr};

        // Create task
        BaseType_t result = xTaskCreate(
            taskFunction,
            "PowerTask",
            TASK_STACK_SIZE,
            ctx,
            TASK_PRIORITY,
            &taskHandle);

        if (result != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create Power task!");
            delete ctx;
            return nullptr;
        }

        ESP_LOGI(TAG, "Power task created successfully");
        return taskHandle;
    }

    TaskHandle_t getHandle()
    {
        return taskHandle;
    }

} // namespace PowerTask
