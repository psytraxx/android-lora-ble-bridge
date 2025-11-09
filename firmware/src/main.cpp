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
//! - Deep sleep for power optimization
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
#include <esp_wifi.h>
#include <esp_bt.h>
#include <Adafruit_SleepyDog.h>

// RTC memory - persists across deep sleep
RTC_DATA_ATTR int bootCount = 0;

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
void onLoRaTransmitComplete(bool success);

/**
 * @brief Setup routine for ESP32 LoRa-BLE Bridge
 */
void setup()
{
    Serial.begin(115200);

    bootCount++; // Increment boot counter (persists in RTC memory)

    // Print wakeup reason and boot count
    Serial.printf("Boot count: %d\n", bootCount);
    PowerManager::printWakeupReason();

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
    // Note: NimBLE uses the BLE controller, so we only release Classic BT memory
    esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT);
    Serial.println("Bluetooth Classic memory released (using NimBLE for BLE only)");

    // Reconfigure watchdog timer with sufficient timeout (Arduino ESP32 v2.0+)
    Watchdog.enable(WatchdogConstants::TIMEOUT_SECONDS * 1000); // timeout in milliseconds

    Serial.printf("ESP32 LoRa-BLE Bridge starting\n");

    // Initialize message buffer (NVS-backed, persists across deep sleep)
    if (!messageBuffer.begin())
    {
        Serial.printf("Failed to initialize message buffer. Halting execution.\n");
        while (1)
        {
            delay(LoRaConstants::INIT_RETRY_DELAY_MS);
        }
    }

    Serial.printf("Message buffer initialized with %d persisted messages\n", messageBuffer.getCount());

    // Initialize LoRa Manager
    loraManager = new LoRaManager(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS, LORA_RST, LORA_DIO0, LORA_BUSY);

    // Configure LoRa parameters
    LoRaConfig loraConfig = {
        .frequency = LORA_FREQUENCY,
        .bandwidth = LORA_BANDWIDTH,
        .spreadingFactor = LORA_SPREADING_FACTOR,
        .codingRate = LORA_CODING_RATE,
        .txPower = LORA_TX_POWER,
        .syncWord = LoRaConstants::SYNC_WORD};

    // Initialize LoRa radio with retry logic
    if (!loraManager->begin(loraConfig))
    {
        Serial.println("LoRa setup failed permanently. Halting execution.");    
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(LoRaConstants::INIT_RETRY_DELAY_MS));
        }
    }

    // Set callbacks for LoRa events
    loraManager->setReceiveCallback(onLoRaPacketReceived);
    loraManager->setTransmitCallback(onLoRaTransmitComplete);

    // Start continuous receive mode
    if (!loraManager->startReceive())
    {
        Serial.println("Failed to start receive mode. Halting execution.");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(LoRaConstants::INIT_RETRY_DELAY_MS));
        }
    }

     // Create message queues
    bleToLoraQueue = xQueueCreate(QueueConstants::BLE_TO_LORA_SIZE, sizeof(Message));
    loraToBleQueue = xQueueCreate(QueueConstants::LORA_TO_BLE_SIZE, sizeof(Message));

    if (bleToLoraQueue == nullptr || loraToBleQueue == nullptr)
    {
        Serial.println("Failed to create message queues. Halting execution.");
        while (1)
        {
            delay(BLEConstants::INIT_RETRY_DELAY_MS);
        }
    }

    // Initialize BLE with queue
    bleManager = new BLEManager(bleToLoraQueue);

    // Initialize BLE with retry logic
    int bleRetries = BLEConstants::INIT_RETRY_COUNT;
    bool bleSuccess = false;

    while (bleRetries > 0 && !bleSuccess)
    {
        Serial.printf("BLE setup attempt %d/%d\n", BLEConstants::INIT_RETRY_COUNT - bleRetries + 1, BLEConstants::INIT_RETRY_COUNT);

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
                delay(BLEConstants::INIT_RETRY_DELAY_MS);
            }
            bleRetries--;
        }
    }

    if (!bleSuccess)
    {
        Serial.println("BLE setup failed permanently. Halting execution.");
        while (1)
        {
            delay(BLEConstants::INIT_RETRY_DELAY_MS);
        }
    }

    bleManager->startAdvertising();

    // Configure GPIO wake-up for LoRa interrupt (allows wake from light sleep)
    PowerManager::configureWakeupSources(WAKE_BUTTON, LORA_DIO0);

    // Initialize application controller
    appController.begin(bleManager, loraManager, &messageBuffer, bleToLoraQueue, loraToBleQueue);

    // Initialize LED
#ifdef LED_PIN
    ledManager.setup();
#endif

    Serial.printf("All systems initialized\n");
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
        Serial.printf("LoRa to BLE queue full, dropping %s\n", msgTypeName);
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

    Serial.printf("Deserialized: type=%d\n", (int)msg.type);

    // Handle message types
    switch (msg.type)
    {
    case MessageType::Text:
    {
        Serial.printf("Text - seq: %d, text: \"%s\"\n", msg.textData.seq, msg.textData.text);

        if (msg.textData.hasGps)
        {
            Serial.printf(", GPS: %f°, %f°\n", msg.textData.lat / 1000000.0, msg.textData.lon / 1000000.0);
        }

        // Send ACK
        Message ack = Message::createAck(msg.textData.seq);
        uint8_t ackBuf[64]; // 64 bytes: enough for any message type (ACK=2, Text+GPS=52)
        int ackLen = ack.serialize(ackBuf, sizeof(ackBuf));

        if (ackLen > 0)
        {
            Serial.printf("Sending ACK for seq: %d\n", msg.textData.seq);

            // Wait before sending ACK to ensure sender has switched to RX mode
            delay(LoRaConstants::ACK_DELAY_MS);

            // Reset watchdog before long LoRa transmission
            Watchdog.reset();

            // Start non-blocking transmission via LoRaManager
            loraManager->startTransmit(ackBuf, ackLen);
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
        Serial.printf("ACK - seq: %d\n", msg.ackData.seq);

        // Queue or buffer ACK for BLE delivery
        queueOrBufferMessage(msg, "ACK");

#ifdef LED_PIN
        ledManager.blink();
#endif
        break;
    }

    case MessageType::WakeUp:
    {
        Serial.printf("WakeUp message received");
        // Wake-up messages don't need to be forwarded to BLE
        // They are used to wake devices from deep sleep via LoRa
        break;
    }

    default:
    {
        Serial.printf("Unknown message type: %d\n", (int)msg.type);
        break;
    }
    }
}

/**
 * @brief Callback for LoRa transmission completion (called from LoRaManager)
 */
void onLoRaTransmitComplete(bool success)
{
    if (success)
    {
        Serial.println("LoRa transmission completed successfully");
    }
    else
    {
        Serial.println("LoRa transmission failed");
    }
}

/**
 * @brief Main loop - thin coordinator for components and state machine
 */
void loop()
{
    // Reset watchdog
    Watchdog.reset();

    // Process component events (non-blocking)
    bleManager->process();
    loraManager->process();

    // Application state machine handles all logic
    appController.update();

    // Adaptive delay based on activity (managed by ApplicationController)
    delay(appController.getLoopDelay());
}