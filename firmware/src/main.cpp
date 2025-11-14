//! ESP32 Firmware for LoRa-BLE Bridge (FreeRTOS Multi-Task Architecture)
//!
//! This firmware implements a BLE peripheral that communicates with Android devices
//! and bridges BLE messages to LoRa transmission and reception.
//!
//! REFACTORED ARCHITECTURE:
//! - ApplicationController: Pure state machine (thread-safe)
//! - BLE Task (Priority 3): Processes LoRa→BLE messages, forwards buffered messages
//! - LoRa Task (Priority 4): Processes BLE→LoRa messages, handles RX/TX events
//! - Power Task (Priority 2): Monitors timeouts, triggers sleep/disconnect
//!
//! Features:
//! - True FreeRTOS multi-tasking (parallel BLE and LoRa processing)
//! - Event-driven architecture (task notifications instead of polling)
//! - Thread-safe state management (mutex-protected ApplicationController)
//! - Simple, testable components (single responsibility per task)
//! - Power optimization (deep sleep, light sleep, adaptive delays)

#include "BLEManager.h"
#include "LoRaManager.h"
#include "Protocol.h"
#include "LEDManager.h"
#include "MessageBuffer.h"
#include "FirmwareConfig.h"
#include "ApplicationController.h"
#include "PowerManager.h"

// FreeRTOS tasks
#include "BleTask.h"
#include "LoraTask.h"
#include "PowerTask.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "esp_log.h"
#include <esp_task_wdt.h>

// RTC memory - persists across deep sleep
RTC_DATA_ATTR int bootCount = 0;

// Component instances
static LoRaManager *loraManager = nullptr;
static BLEManager *bleManager = nullptr;

#ifdef LED_PIN
LEDManager ledManager(LED_PIN);
#endif

// Message queues (created in setup, accessed by tasks)
static QueueHandle_t bleToLoraQueue = nullptr;
static QueueHandle_t loraToBleQueue = nullptr;

// Message buffer for when BLE is disconnected (NVS-backed)
static MessageBuffer messageBuffer;

// Application state machine (thread-safe, accessed by all tasks)
static ApplicationController appController;

// Task handles
static TaskHandle_t bleTaskHandle = nullptr;
static TaskHandle_t loraTaskHandle = nullptr;
static TaskHandle_t powerTaskHandle = nullptr;

static const char *TAG = "Main";

// ============================================================================
// BLE Connection Callbacks (integrate with ApplicationController and tasks)
// ============================================================================

/**
 * @brief Forward declarations for BLE connection callbacks
 * These will be called by BLEManager when connection state changes
 */
void onBleConnected();
void onBleDisconnected();

// ============================================================================
// Initialization
// ============================================================================

void setup()
{
    bootCount++; // Increment boot counter (persists in RTC memory)

    ESP_LOGI(TAG, "=================================================================");
    ESP_LOGI(TAG, "ESP32 LoRa-BLE Bridge - FreeRTOS Multi-Task Architecture");
    ESP_LOGI(TAG, "Boot count: %d", bootCount);
    ESP_LOGI(TAG, "=================================================================");

    PowerManager::printWakeupReason();

    // ========================================================================
    // Power Management & Hardware Initialization
    // ========================================================================

    // Configure power management (CPU frequency scaling)
    PowerManager::configurePowerManagement();

    PowerManager::disableWiFi();

    PowerManager::disableBluetoothClassic();

    // Configure watchdog timer with sufficient timeout for LoRa operations
    esp_task_wdt_config_t wdtConfig = {
        .timeout_ms = WatchdogConstants::TIMEOUT_SECONDS * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true};
    esp_task_wdt_deinit();
    esp_task_wdt_init(&wdtConfig);
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG, "Watchdog configured: %d s timeout", WatchdogConstants::TIMEOUT_SECONDS);

    // ========================================================================
    // Component Initialization
    // ========================================================================

    // Initialize message buffer (NVS-backed, persists across deep sleep)
    if (!messageBuffer.begin())
    {
        ESP_LOGE(TAG, "Failed to initialize message buffer!");
        ESP_LOGE(TAG, "FATAL: Cannot continue. Halting.");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    ESP_LOGI(TAG, "Message buffer initialized (%d persisted messages)", messageBuffer.getCount());

    // Initialize LoRa Manager
    loraManager = new LoRaManager(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS, LORA_RST, LORA_DIO0, LORA_BUSY);

    LoRaConfig loraConfig = {
        .frequency = LORA_FREQUENCY,
        .bandwidth = LORA_BANDWIDTH,
        .spreadingFactor = LORA_SPREADING_FACTOR,
        .codingRate = LORA_CODING_RATE,
        .txPower = LORA_TX_POWER,
        .syncWord = LoRaConstants::SYNC_WORD};

    if (!loraManager->begin(loraConfig))
    {
        ESP_LOGE(TAG, "LoRa initialization failed!");
        ESP_LOGE(TAG, "FATAL: Cannot continue. Halting.");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Start continuous receive mode
    if (!loraManager->startReceive())
    {
        ESP_LOGE(TAG, "Failed to start LoRa receive mode!");
        ESP_LOGE(TAG, "FATAL: Cannot continue. Halting.");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "LoRa initialized (listening on %.2f MHz)", LORA_FREQUENCY);

    // Create message queues
    bleToLoraQueue = xQueueCreate(QueueConstants::BLE_TO_LORA_SIZE, sizeof(Message));
    loraToBleQueue = xQueueCreate(QueueConstants::LORA_TO_BLE_SIZE, sizeof(Message));

    if (bleToLoraQueue == nullptr || loraToBleQueue == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create message queues!");
        ESP_LOGE(TAG, "FATAL: Cannot continue. Halting.");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    ESP_LOGI(TAG, "Message queues created (BLE→LoRa: %d, LoRa→BLE: %d)",
             QueueConstants::BLE_TO_LORA_SIZE, QueueConstants::LORA_TO_BLE_SIZE);

    // Initialize BLE Manager
    bleManager = new BLEManager(bleToLoraQueue);

    int bleRetries = BLEConstants::INIT_RETRY_COUNT;
    bool bleSuccess = false;

    while (bleRetries > 0 && !bleSuccess)
    {
        ESP_LOGI(TAG, "BLE initialization attempt %d/%d",
                 BLEConstants::INIT_RETRY_COUNT - bleRetries + 1, BLEConstants::INIT_RETRY_COUNT);

        if (bleManager->setup(DEVICE_NAME))
        {
            bleSuccess = true;
            ESP_LOGI(TAG, "BLE initialized successfully");
        }
        else
        {
            ESP_LOGE(TAG, "BLE initialization failed");
            if (bleRetries > 1)
            {
                ESP_LOGI(TAG, "Retrying in 2 seconds...");
                vTaskDelay(pdMS_TO_TICKS(BLEConstants::INIT_RETRY_DELAY_MS));
            }
            bleRetries--;
        }
    }

    if (!bleSuccess)
    {
        ESP_LOGE(TAG, "BLE initialization failed permanently!");
        ESP_LOGE(TAG, "FATAL: Cannot continue. Halting.");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Register BLE connection callbacks (declared below)
    bleManager->setConnectionCallbacks(onBleConnected, onBleDisconnected);

    // Start BLE advertising
    bleManager->startAdvertising();
    ESP_LOGI(TAG, "BLE advertising started");

    // Configure GPIO wake-up sources (LoRa interrupt, wake button)
    PowerManager::configureWakeupSources(WAKE_BUTTON, LORA_DIO0);

    // Initialize ApplicationController (pure state machine)
    appController.begin();

    // Initialize LED
#ifdef LED_PIN
    ledManager.setup();
    ESP_LOGI(TAG, "LED initialized");
#endif

    // ========================================================================
    // Create FreeRTOS Tasks
    // ========================================================================

    ESP_LOGI(TAG, "Creating FreeRTOS tasks...");

    // Create BLE task (priority 3)
    bleTaskHandle = BleTask::start(&appController, bleManager, &messageBuffer, loraToBleQueue);
    if (bleTaskHandle == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create BLE task!");
        ESP_LOGE(TAG, "FATAL: Cannot continue. Halting.");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Create LoRa task (priority 4 - highest)
    loraTaskHandle = LoraTask::start(&appController, loraManager, &messageBuffer, bleToLoraQueue, loraToBleQueue);
    if (loraTaskHandle == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create LoRa task!");
        ESP_LOGE(TAG, "FATAL: Cannot continue. Halting.");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Create Power task (priority 2 - lowest)
    powerTaskHandle = PowerTask::start(&appController, bleManager);
    if (powerTaskHandle == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create Power task!");
        ESP_LOGE(TAG, "FATAL: Cannot continue. Halting.");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "=================================================================");
    ESP_LOGI(TAG, "All tasks created successfully!");
    ESP_LOGI(TAG, "  - BLE Task (Priority 3): Message processing and buffering");
    ESP_LOGI(TAG, "  - LoRa Task (Priority 4): Radio TX/RX and message processing");
    ESP_LOGI(TAG, "  - Power Task (Priority 2): Timeout monitoring and power mgmt");
    ESP_LOGI(TAG, "=================================================================");

    // Unregister main task from watchdog (tasks will manage their own watchdogs)
    esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG, "Main task unregistered from watchdog");

    ESP_LOGI(TAG, "System ready. FreeRTOS scheduler running.");
}

// ============================================================================
// BLE Connection Callbacks
// ============================================================================

void onBleConnected()
{
    ESP_LOGI(TAG, "BLE connection established");

    // Update state machine
    appController.onBleConnected();

    // Notify BLE task to forward buffered messages
    BleTask::notifyConnectionChange(true);
}

void onBleDisconnected()
{
    ESP_LOGI(TAG, "BLE connection lost");

    // Update state machine
    appController.onBleDisconnected();

    // Notify BLE task
    BleTask::notifyConnectionChange(false);
}

// ============================================================================
// Main Loop (No Longer Used - Kept for Compatibility)
// ============================================================================

/**
 * @brief Main loop is no longer used in FreeRTOS architecture
 *
 * All logic has been moved to dedicated tasks:
 *  - BLE Task handles BLE message processing
 *  - LoRa Task handles LoRa operations
 *  - Power Task handles timeout monitoring
 *
 * This loop is kept empty for compatibility with ESP-IDF Arduino framework,
 * but does nothing. The FreeRTOS scheduler manages all tasks automatically.
 */
void loop()
{
    // Sleep forever - FreeRTOS tasks handle everything
    vTaskDelay(portMAX_DELAY);
}

// ============================================================================
// ESP-IDF Entry Point
// ============================================================================

extern "C" void app_main(void)
{
    // Call setup once
    setup();

    // Run main loop (which now just sleeps forever)
    while (1)
    {
        loop();
    }
}
