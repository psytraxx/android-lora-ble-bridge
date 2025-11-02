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
#include <Arduino.h>
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

/**
 * @brief Setup routine for ESP32 LoRa-BLE Bridge
 */
void setup()
{
    Serial.begin(115200);

    Serial.println("Disabling WiFi and Bluetooth Classic for power savings");

    // Configure power management (CPU frequency scaling and light sleep)
    PowerManager::configurePowerManagement();

    // Disable WiFi completely (saves ~50-80 mA)
    // WiFi is initialized by default in ESP32 Arduino framework
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_INIT)
    {
        esp_wifi_deinit();
        Serial.println("WiFi disabled successfully");
    }
    else
    {
        Serial.printf("WiFi stop failed: %d (may not be initialized)\n", err);
    }

    // Disable Bluetooth Classic (we only use BLE via NimBLE)
    // Note: NimBLE doesn't use the classic Bluetooth stack
    btStop();
    esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT);
    Serial.println("Bluetooth Classic disabled (using NimBLE for BLE only)");

    // Initialize watchdog timer once with sufficient timeout for longest operation
    esp_task_wdt_init(WatchdogConstants::TIMEOUT_SECONDS, true);
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    Serial.print("Watchdog timer initialized with ");
    Serial.print(WatchdogConstants::TIMEOUT_SECONDS);
    Serial.println("s timeout");

    Serial.println("ESP32 LoRa-BLE Bridge starting");

    // Create message queues
    bleToLoraQueue = xQueueCreate(QueueConstants::BLE_TO_LORA_SIZE, sizeof(Message));
    loraToBleQueue = xQueueCreate(QueueConstants::LORA_TO_BLE_SIZE, sizeof(Message));

    if (bleToLoraQueue == nullptr || loraToBleQueue == nullptr)
    {
        Serial.println("Failed to create message queues. Halting execution.");
        while (1)
        {
            delay(1000);
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
        Serial.print("BLE setup attempt ");
        Serial.print(BLE_RETRY_COUNT - bleRetries + 1);
        Serial.print("/");
        Serial.println(BLE_RETRY_COUNT);

        if (bleManager->setup(DEVICE_NAME))
        {
            bleSuccess = true;
            Serial.println("BLE setup successful");
        }
        else
        {
            Serial.println("BLE setup failed");
            if (bleRetries > 1)
            {
                Serial.println("Retrying in 2 seconds...");
                delay(2000);
            }
            bleRetries--;
        }
    }

    if (!bleSuccess)
    {
        Serial.println("BLE setup failed permanently. Halting execution.");
        while (1)
        {
            delay(1000);
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
        Serial.println("LoRa setup failed permanently. Halting execution.");
        while (1)
        {
            delay(1000);
        }
    }

    // Set callback for received LoRa packets
    loraManager->setReceiveCallback(onLoRaPacketReceived);

    // Start continuous receive mode
    if (!loraManager->startReceive())
    {
        Serial.println("Failed to start receive mode. Halting execution.");
        while (1)
        {
            delay(1000);
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

    Serial.println("All systems initialized");
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
        Serial.print("Warning: LoRa to BLE queue full, dropping ");
        Serial.println(msgTypeName);
    }
}

/**
 * @brief Process received LoRa packet (callback from LoRaManager)
 */
void onLoRaPacketReceived(const LoRaPacket &packet)
{
    Serial.println("onLoRaPacketReceived: packet received");

    // Notify application controller of activity
    appController.notifyActivity();

    // Deserialize message
    Message msg;
    if (!msg.deserialize(packet.buffer, packet.len))
    {
        Serial.println("Failed to deserialize LoRa message");
        return;
    }

    Serial.print("Deserialized: type=");
    Serial.println((int)msg.type);

    // Handle message types
    switch (msg.type)
    {
    case MessageType::Text:
    {
        Serial.print("Text - seq: ");
        Serial.print(msg.textData.seq);
        Serial.print(", text: \"");
        Serial.print(msg.textData.text);
        Serial.print("\"");

        if (msg.textData.hasGps)
        {
            Serial.print(", GPS: ");
            Serial.print(msg.textData.lat / 1000000.0, 6);
            Serial.print("°, ");
            Serial.print(msg.textData.lon / 1000000.0, 6);
            Serial.print("°");
        }
        Serial.println();

        // Send ACK
        Message ack = Message::createAck(msg.textData.seq);
        uint8_t ackBuf[64]; // 64 bytes: enough for any message type (ACK=2, Text+GPS=52)
        int ackLen = ack.serialize(ackBuf, sizeof(ackBuf));

        if (ackLen > 0)
        {
            Serial.print("Sending ACK for seq: ");
            Serial.println(msg.textData.seq);

            // Wait before sending ACK to ensure sender has switched to RX mode
            delay(LoRaConstants::ACK_DELAY_MS);

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
        Serial.print("ACK - seq: ");
        Serial.println(msg.ackData.seq);

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
