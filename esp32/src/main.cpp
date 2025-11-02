//! ESP32 Firmware for LoRa-BLE Bridge (Power-Optimized)
//!
//! This firmware implements a BLE peripheral that communicates with Android devices
//! and bridges BLE messages to LoRa transmission and reception.
//!
//! Features:
//! - BLE GATT server with TX/RX characteristics for message exchange
//! - LoRa radio for long-range communication (5-10 km typical)
//! - Message queue for inter-task communication
//! - Message buffering (up to 10 messages) when BLE disconnected
//! - Light sleep for power optimization
//! - Interrupt-driven LoRa reception (always listening)
#include "BLEManager.h"
#include "LoRaManager.h"
#include "Protocol.h"
#include "LEDManager.h"
#include "MessageBuffer.h"
#include "FirmwareConfig.h"
#include "ApplicationController.h"
#include "PowerManager.h"
#include <freertos/queue.h>
#include <esp_task_wdt.h>
#include <freertos/task.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include "esp_log.h"

// Component instances
LoRaManager *loraManager;
BLEManager *bleManager;

#ifdef LED_PIN
LEDManager ledManager(LED_PIN);
#endif

// Message queues
QueueHandle_t bleToLoraQueue;
QueueHandle_t loraToBleQueue;

// Message buffer for when BLE is disconnected
MessageBuffer messageBuffer;

// Application state machine
ApplicationController appController;

// Forward declaration
void onLoRaPacketReceived(const LoRaPacket &packet);

static const char *TAG_MAIN = "Main";

/**
 * @brief Setup routine for ESP32 LoRa-BLE Bridge
 */
void setup()
{
    ESP_LOGI(TAG_MAIN, "Disabling WiFi and Bluetooth Classic for power savings");

    // Configure power management (CPU frequency scaling and light sleep)
    PowerManager::configurePowerManagement();

    // Disable WiFi completely (saves ~50-80 mA)
    // WiFi is initialized by default in ESP32 Arduino framework
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_INIT)
    {
        esp_wifi_deinit();
        ESP_LOGI(TAG_MAIN, "WiFi disabled successfully");
    }
    else
    {
        ESP_LOGE(TAG_MAIN, "WiFi stop failed: %d (may not be initialized)", err);
    }

    // Disable Bluetooth Classic (we only use BLE via NimBLE)
    // Note: NimBLE uses the BLE controller, so we only release Classic BT memory
    esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT);
    ESP_LOGI(TAG_MAIN, "Bluetooth Classic memory released (using NimBLE for BLE only)");

    // Initialize watchdog timer once with sufficient timeout for longest operation
    esp_task_wdt_config_t wdtConfig = {
        .timeout_ms = WatchdogConstants::TIMEOUT_SECONDS * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true};
    esp_task_wdt_init(&wdtConfig);
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG_MAIN, "Watchdog timer initialized with %d s timeout", WatchdogConstants::TIMEOUT_SECONDS);

    ESP_LOGI(TAG_MAIN, "ESP32 LoRa-BLE Bridge starting");

    // Create message queues
    bleToLoraQueue = xQueueCreate(QueueConstants::BLE_TO_LORA_SIZE, sizeof(Message));
    loraToBleQueue = xQueueCreate(QueueConstants::LORA_TO_BLE_SIZE, sizeof(Message));

    if (bleToLoraQueue == nullptr || loraToBleQueue == nullptr)
    {
        ESP_LOGE(TAG_MAIN, "Failed to create message queues. Halting execution.");
        while (1)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    // Initialize BLE with queue
    bleManager = new BLEManager(bleToLoraQueue);

    // Initialize BLE with retry logic
    const int BLE_RETRY_COUNT = 3;
    int bleRetries = BLE_RETRY_COUNT;
    bool bleSuccess = false;

    while (bleRetries > 0 && !bleSuccess)
    {
        ESP_LOGI(TAG_MAIN, "BLE setup attempt %d/%d", BLE_RETRY_COUNT - bleRetries + 1, BLE_RETRY_COUNT);

        if (bleManager->setup(DEVICE_NAME))
        {
            bleSuccess = true;
            ESP_LOGI(TAG_MAIN, "BLE setup successful");
        }
        else
        {
            ESP_LOGE(TAG_MAIN, "BLE setup failed");
            if (bleRetries > 1)
            {
                ESP_LOGI(TAG_MAIN, "Retrying in 2 seconds...");
                vTaskDelay(2000 / portTICK_PERIOD_MS);
            }
            bleRetries--;
        }
    }

    if (!bleSuccess)
    {
        ESP_LOGE(TAG_MAIN, "BLE setup failed permanently. Halting execution.");
        while (1)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    bleManager->startAdvertising();

    // Initialize LoRa Manager
    loraManager = new LoRaManager(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS, LORA_RST, LORA_DIO0);

    // Configure LoRa parameters
    LoRaConfig loraConfig = {
        .frequency = LORA_FREQUENCY,
        .bandwidth = LORA_BANDWIDTH,
        .spreadingFactor = LORA_SPREADING_FACTOR,
        .codingRate = LORA_CODING_RATE,
        .txPower = LORA_TX_POWER,
        .syncWord = LoRaConstants::SYNC_WORD};

    // Initialize LoRa radio with retry logic
    if (!loraManager->begin(loraConfig, 3))
    {
        ESP_LOGE(TAG_MAIN, "LoRa setup failed permanently. Halting execution.");
        while (1)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    // Set callback for received LoRa packets
    loraManager->setReceiveCallback(onLoRaPacketReceived);

    // Start continuous receive mode
    if (!loraManager->startReceive())
    {
        ESP_LOGE(TAG_MAIN, "Failed to start receive mode. Halting execution.");
        while (1)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    // Configure GPIO wake-up for LoRa interrupt (allows wake from light sleep)
    PowerManager::configureWakeupSources(WAKE_BUTTON, LORA_DIO0);

    // Initialize application controller
    appController.begin(bleManager, loraManager, &messageBuffer, bleToLoraQueue, loraToBleQueue);

    // Initialize LED
#ifdef LED_PIN
    ledManager.setup();
#endif

    ESP_LOGI(TAG_MAIN, "All systems initialized");
}

/**
 * @brief Queue message to LoRa→BLE queue (called from LoRa callback)
 * @param msg Message to send
 * @param msgTypeName Human-readable message type for logging
 */
void queueOrBufferMessage(const Message &msg, const char *msgTypeName)
{
    if (xQueueSend(loraToBleQueue, &msg, 0) != pdTRUE)
    {
        ESP_LOGW(TAG_MAIN, "LoRa to BLE queue full, dropping %s", msgTypeName);
    }
}

/**
 * @brief Process received LoRa packet (callback from LoRaManager)
 */
void onLoRaPacketReceived(const LoRaPacket &packet)
{
    ESP_LOGI(TAG_MAIN, "onLoRaPacketReceived: packet received");

    // Notify application controller of activity
    appController.notifyActivity();

    // Deserialize message
    Message msg;
    if (!msg.deserialize(packet.buffer, packet.len))
    {
        ESP_LOGE(TAG_MAIN, "Failed to deserialize LoRa message");
        return;
    }

    ESP_LOGI(TAG_MAIN, "Deserialized: type=%d", (int)msg.type);

    // Handle message types
    switch (msg.type)
    {
    case MessageType::Text:
    {
        ESP_LOGI(TAG_MAIN, "Text - seq: %d, text: \"%s\"", msg.textData.seq, msg.textData.text);

        if (msg.textData.hasGps)
        {
            ESP_LOGI(TAG_MAIN, ", GPS: %f°, %f°", msg.textData.lat / 1000000.0, msg.textData.lon / 1000000.0);
        }

        // Send ACK
        Message ack = Message::createAck(msg.textData.seq);
        uint8_t ackBuf[64]; // 64 bytes: enough for any message type (ACK=2, Text+GPS=52)
        int ackLen = ack.serialize(ackBuf, sizeof(ackBuf));

        if (ackLen > 0)
        {
            ESP_LOGI(TAG_MAIN, "Sending ACK for seq: %d", msg.textData.seq);

            // Wait before sending ACK to ensure sender has switched to RX mode
            vTaskDelay(pdMS_TO_TICKS(LoRaConstants::ACK_DELAY_MS));

            // Reset watchdog before long LoRa transmission
            esp_task_wdt_reset();

            // Transmit ACK via LoRaManager (handles mode switching)
            loraManager->transmit(ackBuf, ackLen);
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
        ESP_LOGI(TAG_MAIN, "ACK - seq: %d", msg.ackData.seq);

        // Queue or buffer ACK for BLE delivery
        queueOrBufferMessage(msg, "ACK");

#ifdef LED_PIN
        ledManager.blink();
#endif
        break;
    }
    }
}

/**
 * @brief Main loop - thin coordinator for components and state machine
 */
void loop()
{
    // Reset watchdog
    esp_task_wdt_reset();

    // Process component events (non-blocking)
    bleManager->process();
    loraManager->process();

    // Application state machine handles all logic
    appController.update();

    // Adaptive delay based on activity (managed by ApplicationController)
    vTaskDelay(pdMS_TO_TICKS(appController.getLoopDelay()));
}

/**
 * @brief ESP-IDF entry point
 *
 * In ESP-IDF, app_main() runs as a task. We call setup() once,
 * then enter the main loop directly without creating another task.
 */
extern "C" void app_main(void)
{
    // Call setup once
    setup();

    // Run main loop continuously
    while (1)
    {
        loop();
    }
}
