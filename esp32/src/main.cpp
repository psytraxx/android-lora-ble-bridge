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
#include <RadioLib.h>
#include "BLEManager.h"
#include "Protocol.h"
#include "LEDManager.h"
#include "MessageBuffer.h"
#include <freertos/queue.h>
#include <esp_task_wdt.h>
#include <freertos/task.h>
#include <esp_wifi.h>
#include "esp_pm.h"
#include <esp_sleep.h>
#include "PowerController.h"

// RadioLib SX1278 radio instance
SX1278 radio = new Module(LORA_SS, LORA_DIO0, LORA_RST, RADIOLIB_NC);
#ifdef LED_PIN
LEDManager ledManager(LED_PIN);
#endif

// Message queues using FreeRTOS
const int BLE_TO_LORA_QUEUE_SIZE = 10;
const int LORA_TO_BLE_QUEUE_SIZE = 15;

QueueHandle_t bleToLoraQueue;
QueueHandle_t loraToBleQueue;

// Struct for LoRa packets with metadata
struct LoRaPacket
{
    uint8_t buffer[256];
    int len;
    int rssi;
    float snr;
};

QueueHandle_t loRaQueue;

// BLEManager declared after queues
BLEManager *bleManager;

// Power controller instance
PowerController powerController;

// Message buffer for when BLE is disconnected (SINGLE GLOBAL INSTANCE)
MessageBuffer messageBuffer;

// Flag for LoRa activity (set in ISR, checked in loop)
volatile bool loraActivity = false;

/**
 * @brief LoRa receive callback - handles incoming LoRa packets event-driven (ISR)
 * Queues the packet for processing in main loop (fast ISR pattern)
 */
#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void onLoRaReceive(void)
{
    // Read packet data in ISR and queue it
    LoRaPacket packet;
    int state = radio.readData(packet.buffer, sizeof(packet.buffer));

    if (state == RADIOLIB_ERR_NONE)
    {
        packet.len = radio.getPacketLength();
        packet.rssi = radio.getRSSI();
        packet.snr = radio.getSNR();

        // Queue the packet for main loop processing
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(loRaQueue, &packet, &xHigherPriorityTaskWoken);
        loraActivity = true;
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    // Restart receive mode
    radio.startReceive();
}

/**
 * @brief Setup routine for ESP32 LoRa-BLE Bridge
 */
void setup()
{
    Serial.begin(115200);

    Serial.println("Disabling WiFi and Bluetooth Classic for power savings");

#if CONFIG_IDF_TARGET_ESP32
    esp_pm_config_esp32_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 10,
        .light_sleep_enable = true,
    };
#elif CONFIG_IDF_TARGET_ESP32S3
    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 10,
        .light_sleep_enable = true,
    };
#endif

    esp_pm_configure(&pm_config);

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
    Serial.println("Bluetooth Classic disabled (using NimBLE for BLE only)");

    // Set initial CPU frequency to match power management max
    setCpuFrequencyMhz(80);
    Serial.print("CPU Frequency: ");
    Serial.print(getCpuFrequencyMhz());
    Serial.println(" MHz");

    Serial.println("Power management configured (light sleep enabled)");

    esp_task_wdt_add(xTaskGetCurrentTaskHandle());

    Serial.println("ESP32 LoRa-BLE Bridge starting");

    // Create message queues
    bleToLoraQueue = xQueueCreate(BLE_TO_LORA_QUEUE_SIZE, sizeof(Message));
    loraToBleQueue = xQueueCreate(LORA_TO_BLE_QUEUE_SIZE, sizeof(Message));
    loRaQueue = xQueueCreate(15, sizeof(LoRaPacket));

    if (bleToLoraQueue == nullptr || loraToBleQueue == nullptr || loRaQueue == nullptr)
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

    // Initialize power controller with BLE manager and message buffer
    powerController.begin(bleManager, &messageBuffer);

    // Initialize LoRa with RadioLib
    Serial.println("Initializing LoRa radio (RadioLib)");

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);

    const int LORA_RETRY_COUNT = 3;
    int loraRetries = LORA_RETRY_COUNT;
    bool loraSuccess = false;

    while (loraRetries > 0 && !loraSuccess)
    {
        Serial.print("LoRa setup attempt ");
        Serial.print(LORA_RETRY_COUNT - loraRetries + 1);
        Serial.print("/");
        Serial.println(LORA_RETRY_COUNT);

        int state = radio.begin(LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODING_RATE, 0x12, LORA_TX_POWER);

        if (state == RADIOLIB_ERR_NONE)
        {
            loraSuccess = true;
            Serial.println("LoRa setup successful");
            Serial.print("  Frequency: ");
            Serial.print(LORA_FREQUENCY);
            Serial.println(" MHz");
            Serial.print("  Bandwidth: ");
            Serial.print(LORA_BANDWIDTH);
            Serial.println(" kHz");
            Serial.print("  Spreading Factor: ");
            Serial.println(LORA_SPREADING_FACTOR);
            Serial.print("  Coding Rate: 4/");
            Serial.println(LORA_CODING_RATE);
            Serial.print("  TX Power: ");
            Serial.print(LORA_TX_POWER);
            Serial.println(" dBm");
        }
        else
        {
            Serial.print("LoRa setup failed, code ");
            Serial.println(state);
            if (loraRetries > 1)
            {
                Serial.println("Retrying in 1 second...");
                delay(1000);
            }
            loraRetries--;
        }
    }

    if (!loraSuccess)
    {
        Serial.println("LoRa setup failed permanently. Halting execution.");
        while (1)
        {
            delay(1000);
        }
    }

    // Set up event-driven LoRa reception (CRITICAL: Always listening)
    radio.setPacketReceivedAction(onLoRaReceive);

    // Start continuous receive mode
    int state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.print("Failed to start receive mode, code ");
        Serial.println(state);
    }

    // Configure GPIO wake-up for LoRa interrupt (allows wake from light sleep)
    gpio_wakeup_enable((gpio_num_t)LORA_DIO0, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    Serial.println("GPIO wake-up enabled for LoRa DIO0 - can wake from light sleep");

    // Initialize LED
#ifdef LED_PIN
    ledManager.setup();
#endif

    Serial.println("All systems initialized");
}

/**
 * @brief Handle LoRa to BLE message forwarding and buffering
 */
void handleLoRaToBleForwarding()
{
    // Immediately send buffered messages on BLE connect
    if (bleManager->isConnected() && !messageBuffer.isEmpty())
    {
        Serial.print("BLE connected - sending ");
        Serial.print(messageBuffer.getCount());
        Serial.println(" buffered messages");

        Message bufferedMsg;
        while (messageBuffer.get(bufferedMsg))
        {
            if (bleManager->sendMessage(bufferedMsg))
            {
                Serial.println("Buffered message sent successfully");
#ifdef LED_PIN
                ledManager.blink();
#endif
                delay(20); // Small delay between messages to avoid overwhelming BLE
            }
            else
            {
                Serial.println("Failed to send buffered message - keeping remaining messages in buffer");
                // Re-add the failed message at the end (simple approach)
                messageBuffer.add(bufferedMsg);
                break; // Stop if send fails
            }
        }
    }

    // Process live queue messages
    Message loraMsg;
    if (xQueueReceive(loraToBleQueue, &loraMsg, 0) == pdTRUE)
    {
        if (bleManager->isConnected())
        {
            if (bleManager->sendMessage(loraMsg))
            {
                Serial.println("Message forwarded from LoRa to BLE");
#ifdef LED_PIN
                ledManager.blink();
#endif
            }
        }
        else
        {
            // Buffer message for later delivery
            messageBuffer.add(loraMsg);
            Serial.print("Buffered message (total: ");
            Serial.print(messageBuffer.getCount());
            Serial.println(")");
        }
    }
}

/**
 * @brief Process received LoRa packet
 */
void processLoRaPacket(const LoRaPacket &packet)
{
    Serial.println("processLoRaPacket: packet received");
    bleManager->updateActivity();

    // If not connected, just note activity; PowerController will manage advertising
    if (!bleManager->isConnected())
    {
        Serial.println("processLoRaPacket: no BLE connection - buffering for later");
    }

    Serial.print("LoRa RX: ");
    Serial.print(packet.len);
    Serial.print(" bytes, RSSI: ");
    Serial.print(packet.rssi);
    Serial.print(" dBm, SNR: ");
    Serial.print(packet.snr);
    Serial.println(" dB");

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
        uint8_t ackBuf[64];
        int ackLen = ack.serialize(ackBuf, sizeof(ackBuf));

        if (ackLen > 0)
        {
            Serial.print("Sending ACK for seq: ");
            Serial.println(msg.textData.seq);

            // Clear RX interrupt handler to allow DIO0 to signal TX completion
            radio.clearPacketReceivedAction();

            // Reconfigure watchdog for 10 seconds
            esp_task_wdt_init(10, true);
            esp_task_wdt_add(xTaskGetCurrentTaskHandle());

            int state = radio.transmit(ackBuf, ackLen);

            // Restore normal watchdog timeout
            esp_task_wdt_init(5, true);
            esp_task_wdt_add(xTaskGetCurrentTaskHandle());

            if (state == RADIOLIB_ERR_NONE)
            {
                Serial.println("ACK sent successfully");
            }
            else
            {
                Serial.print("ACK send failed, code ");
                Serial.println(state);
            }

            // Restore RX interrupt handler and return to RX mode
            radio.setPacketReceivedAction(onLoRaReceive);
            radio.startReceive();
        }

        // Queue or buffer message for BLE delivery
        if (bleManager->isConnected())
        {
            if (xQueueSend(loraToBleQueue, &msg, 0) != pdTRUE)
            {
                Serial.println("Warning: LoRa to BLE queue full, buffering");
                messageBuffer.add(msg);
            }
        }
        else
        {
            messageBuffer.add(msg);
            Serial.print("Buffered text message (total: ");
            Serial.print(messageBuffer.getCount());
            Serial.println(")");
        }

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
        if (bleManager->isConnected())
        {
            if (xQueueSend(loraToBleQueue, &msg, 0) != pdTRUE)
            {
                Serial.println("Warning: LoRa to BLE queue full, buffering");
                messageBuffer.add(msg);
            }
        }
        else
        {
            messageBuffer.add(msg);
            Serial.print("Buffered ACK (total: ");
            Serial.print(messageBuffer.getCount());
            Serial.println(")");
        }

#ifdef LED_PIN
        ledManager.blink();
#endif
        break;
    }
    }

    // If we were woken by GPIO (LoRa) and we're still disconnected, go back to immediate light sleep
    if (!bleManager->isConnected())
    {
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        if (cause == ESP_SLEEP_WAKEUP_GPIO)
        {
            Serial.println("processLoRaPacket: woke from GPIO - re-entering light sleep");
            powerController.enterLightSleepNow(30ULL * 1000000ULL);
        }
    }
}

/**
 * @brief Main loop - handles BLE<->LoRa message bridging with light sleep for power savings
 */
void loop()
{
    // Reset watchdog
    esp_task_wdt_reset();

    // Process BLE events (non-blocking)
    bleManager->process();

    // Check for messages from BLE to send via LoRa
    Message bleMsg;
    if (xQueueReceive(bleToLoraQueue, &bleMsg, 0) == pdTRUE)
    {
        Serial.print("Received from BLE queue: type=");
        Serial.println((int)bleMsg.type);

        // Serialize and send via LoRa
        uint8_t buf[64];
        int len = bleMsg.serialize(buf, sizeof(buf));

        if (len > 0)
        {
            Serial.print("Transmitting ");
            Serial.print(len);
            Serial.println(" bytes via LoRa");

            // Clear RX interrupt handler to allow DIO0 to signal TX completion
            radio.clearPacketReceivedAction();

            // Reconfigure watchdog for 10 seconds to allow long LoRa transmission
            esp_task_wdt_init(10, true);
            esp_task_wdt_add(xTaskGetCurrentTaskHandle());

            int state = radio.transmit(buf, len);

            // Restore normal watchdog timeout (5 seconds)
            esp_task_wdt_init(5, true);
            esp_task_wdt_add(xTaskGetCurrentTaskHandle());

            if (state == RADIOLIB_ERR_NONE)
            {
                Serial.println("LoRa TX successful");
#ifdef LED_PIN
                ledManager.blink(2);
#endif
            }
            else
            {
                Serial.print("LoRa TX failed, code ");
                Serial.println(state);
            }

            // Restore RX interrupt handler and return to RX mode
            radio.setPacketReceivedAction(onLoRaReceive);
            radio.startReceive();
            delay(50);
        }
        else
        {
            Serial.println("Failed to serialize message for LoRa TX");
        }
    }

    // Check for LoRa packets (queued from ISR)
    LoRaPacket packet;
    if (xQueueReceive(loRaQueue, &packet, 0) == pdTRUE)
    {
        processLoRaPacket(packet);
        loraActivity = false;
    }

    // Forward queued/buffered messages from LoRa to BLE
    handleLoRaToBleForwarding();

    // Power controller manages advertise/sleep cycle and inactivity
    powerController.update();

    // No unconditional immediate sleep here; PowerController manages scheduled sleep.

    // Determine if there is pending activity
    bool hasActivity = uxQueueMessagesWaiting(bleToLoraQueue) > 0 ||
                       uxQueueMessagesWaiting(loRaQueue) > 0 ||
                       loraActivity;

    // Adaptive delay for power savings
    // With automatic light sleep enabled, longer delays allow the system to
    // enter light sleep mode for significant power savings

    if (hasActivity)
    {
        // Activity detected - short delay for responsiveness
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    else
    {
        // Idle - long delay enables automatic light sleep
        // BLE modem and LoRa GPIO interrupts will wake the system
        vTaskDelay(pdMS_TO_TICKS(2000)); // 2 seconds (was 100ms)
    }
}
