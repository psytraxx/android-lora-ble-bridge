//! ESP32 Firmware for LoRa-BLE Bridge (Simplified Architecture)
//!
//! This firmware implements a BLE peripheral that communicates with Android devices
//! and bridges BLE messages to LoRa transmission and reception.
//!
//! SIMPLIFIED ARCHITECTURE (matches nRF52):
//! - Single loop() - no FreeRTOS task complexity
//! - Event-driven: LoRaManager and BLEManager handle callbacks
//! - Simple message queues (no FreeRTOS queues needed)
//! - Same functionality, cleaner code
//!
//! Features:
//! - BLE communication with Android app
//! - LoRa radio TX/RX with interrupt handling
//! - Persistent message buffering (NVS)
//! - Battery monitoring and power management
//! - Protocol-compatible with nRF52 firmware

#include "BLEManager.h"
#include "LoRaManager.h"
#include "Protocol.h"
#include "LEDManager.h"
#include "MessageBuffer.h"
#include "FirmwareConfig.h"
#include "ApplicationController.h"
#include "PowerManager.h"

#include "esp_log.h"
#include <esp_task_wdt.h>
#include <esp_timer.h>

// RTC memory - persists across deep sleep
RTC_DATA_ATTR int bootCount = 0;

// Global managers
static LoRaManager *loraManager = nullptr;
static BLEManager *bleManager = nullptr;
static MessageBuffer *messageBuffer = nullptr;
static ApplicationController *appController = nullptr;

#ifdef LED_PIN
LEDManager ledManager(LED_PIN);
#endif

// Simple message queue (no FreeRTOS queues)
struct MessageQueue
{
    Message messages[QueueConstants::BLE_TO_LORA_SIZE];
    int head;
    int tail;
    int count;

    MessageQueue() : head(0), tail(0), count(0) {}

    bool push(const Message &msg)
    {
        if (count >= QueueConstants::BLE_TO_LORA_SIZE)
            return false;
        messages[tail] = msg;
        tail = (tail + 1) % QueueConstants::BLE_TO_LORA_SIZE;
        count++;
        return true;
    }

    bool pop(Message &msg)
    {
        if (count == 0)
            return false;
        msg = messages[head];
        head = (head + 1) % QueueConstants::BLE_TO_LORA_SIZE;
        count--;
        return true;
    }

    bool isEmpty() const { return count == 0; }
};

static MessageQueue bleToLoraQueue;
static MessageQueue loraToBleQueue;

// Timing variables
static unsigned long lastBatteryUpdate = 0;
static const unsigned long BATTERY_UPDATE_INTERVAL = 60000; // 1 minute

static const char *TAG = "Main";

// Forward declarations
void onBleConnected();
void onBleDisconnected();
void onLoRaReceived(const LoRaPacket &packet);
void onLoRaTransmitted(bool success);
void handleBleMessage(const Message &msg);

// ============================================================================
// Initialization
// ============================================================================

void setup()
{
    bootCount++;

    ESP_LOGI(TAG, "=================================================================");
    ESP_LOGI(TAG, "ESP32 LoRa-BLE Bridge - Simplified Architecture");
    ESP_LOGI(TAG, "Device: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "Boot count: %d", bootCount);
    ESP_LOGI(TAG, "=================================================================");

    PowerManager::printWakeupReason();

    // Power management
    PowerManager::configurePowerManagement();

    // Initialize LED
#ifdef LED_PIN
    ledManager.setup();
    ledManager.setOn();
#endif

    // Configure watchdog (ESP-IDF requirement)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WatchdogConstants::TIMEOUT_SECONDS * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true};
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL); // Add current task

    // Initialize application controller
    appController = new ApplicationController();

    // Initialize message buffer (persistent storage)
    messageBuffer = new MessageBuffer();
    if (!messageBuffer->begin())
    {
        ESP_LOGE(TAG, "Message buffer initialization failed!");
    }

    // Initialize BLE manager
    bleManager = new BLEManager();
    if (!bleManager->setup(DEVICE_NAME))
    {
        ESP_LOGE(TAG, "BLE initialization failed!");
        while (1)
            ;
    }
    bleManager->setConnectionCallbacks(onBleConnected, onBleDisconnected);
    bleManager->setMessageCallback(handleBleMessage);
    bleManager->startAdvertising();

    // Initialize LoRa manager
    loraManager = new LoRaManager(
        LORA_SCK,
        LORA_MISO,
        LORA_MOSI,
        LORA_SS,
        LORA_RST,
        LORA_DIO0,
        LORA_BUSY);

    LoRaConfig loraConfig = {
        .frequency = LORA_FREQUENCY,
        .bandwidth = LORA_BANDWIDTH,
        .spreadingFactor = LORA_SPREADING_FACTOR,
        .codingRate = LORA_CODING_RATE,
        .txPower = LORA_TX_POWER};

    if (!loraManager->begin(loraConfig))
    {
        ESP_LOGE(TAG, "LoRa initialization failed!");
        while (1)
            ;
    }

    loraManager->setReceiveCallback(onLoRaReceived);
    loraManager->setTransmitCallback(onLoRaTransmitted);

    // Start LoRa receive mode (duty cycle for power saving)
    if (!loraManager->startReceive(true))
    {
        ESP_LOGE(TAG, "Failed to start LoRa receive mode!");
    }

#ifdef LED_PIN
    ledManager.setOff();
#endif

    ESP_LOGI(TAG, "Setup complete!");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop()
{
    // Reset watchdog
    esp_task_wdt_reset();

    // Process LoRa events (RX/TX completion)
    loraManager->process();

    // Forward messages from LoRa to BLE
    Message msg;
    if (!loraToBleQueue.isEmpty())
    {
        if (loraToBleQueue.pop(msg))
        {
            if (bleManager->isConnected())
            {
                if (bleManager->sendMessage(msg))
                {
                    appController->notifyActivity();
                }
                else
                {
                    ESP_LOGW(TAG, "Failed to send message to BLE");
                }
            }
            else
            {
                // BLE not ready, buffer message for later
                ESP_LOGI(TAG, "BLE not connected, buffering message");
                messageBuffer->add(msg);
            }
        }
    }

    // Send buffered messages when BLE becomes ready
    if (bleManager->isConnected() && !messageBuffer->isEmpty())
    {
        Message bufferedMsg;
        if (messageBuffer->peek(bufferedMsg))
        {
            if (bleManager->sendMessage(bufferedMsg))
            {
                messageBuffer->popFront();
                ESP_LOGI(TAG, "Sent buffered message, %d remaining", messageBuffer->getCount());
                appController->notifyActivity();
            }
        }
    }

    // Forward messages from BLE to LoRa
    if (!bleToLoraQueue.isEmpty())
    {
        if (bleToLoraQueue.pop(msg))
        {
            ESP_LOGI(TAG, "Transmitting BLE message via LoRa");

            uint8_t buffer[BufferConstants::MAX_PROTOCOL_MESSAGE];
            int length = msg.serialize(buffer, sizeof(buffer));

            if (length > 0)
            {
                if (loraManager->startTransmit(buffer, (size_t)length))
                {
                    appController->notifyActivity();
                }
                else
                {
                    ESP_LOGW(TAG, "Failed to start LoRa transmission");
                }
            }
            else
            {
                ESP_LOGE(TAG, "Failed to serialize message");
            }
        }
    }

    // Update battery level periodically
    unsigned long now = esp_timer_get_time() / 1000; // Convert microseconds to milliseconds
    if (now - lastBatteryUpdate >= BATTERY_UPDATE_INTERVAL)
    {
        uint8_t batteryLevel = PowerManager::readBatteryLevel();
        bleManager->updateBatteryLevel(batteryLevel);

        ESP_LOGI(TAG, "Battery: %d%%", batteryLevel);

        lastBatteryUpdate = now;
    }

    // Check for inactivity timeout
    if (bleManager->isConnected())
    {
        unsigned long inactiveTime = appController->getInactivityDuration();
        if (inactiveTime > PowerConstants::INACTIVITY_TIMEOUT_MS)
        {
            ESP_LOGI(TAG, "Inactivity timeout - disconnecting BLE");
            bleManager->disconnect();
        }
    }

    // Small delay to prevent busy-waiting
    vTaskDelay(pdMS_TO_TICKS(10));
}

// ============================================================================
// Callback Implementations
// ============================================================================

void onBleConnected()
{
    ESP_LOGI(TAG, "BLE connected");
    appController->onBleConnected();
    appController->notifyActivity();

#ifdef LED_PIN
    ledManager.setOn();
#endif
}

void onBleDisconnected()
{
    ESP_LOGI(TAG, "BLE disconnected");
    appController->onBleDisconnected();

#ifdef LED_PIN
    ledManager.setOff();
#endif
}

void onLoRaReceived(const LoRaPacket &packet)
{
    ESP_LOGI(TAG, "LoRa packet received: %d bytes, RSSI: %d dBm, SNR: %.1f dB",
             packet.len, packet.rssi, packet.snr);

    // Deserialize message
    Message msg;
    if (msg.deserialize(packet.buffer, packet.len))
    {
        ESP_LOGI(TAG, "Message type: %d", (int)msg.type);

        // Forward to BLE
        if (loraToBleQueue.push(msg))
        {
            appController->notifyActivity();
        }
        else
        {
            ESP_LOGW(TAG, "LoRa->BLE queue full!");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to deserialize LoRa message");
    }

#ifdef LED_PIN
    ledManager.blink(LEDConstants::RX_BLINKS);
#endif
}

void onLoRaTransmitted(bool success)
{
    if (success)
    {
        ESP_LOGI(TAG, "LoRa transmission successful");

#ifdef LED_PIN
        ledManager.blink(LEDConstants::TX_BLINKS);
#endif
    }
    else
    {
        ESP_LOGW(TAG, "LoRa transmission failed");
    }
}

// ============================================================================
// BLE Message Handler (called by BLEManager)
// ============================================================================

// BLEManager needs access to the queue
void handleBleMessage(const Message &msg)
{
    if (!bleToLoraQueue.push(msg))
    {
        ESP_LOGW(TAG, "BLE->LoRa queue full");
    }
    else
    {
        ESP_LOGI(TAG, "BLE message queued for LoRa, type: %d", (int)msg.type);
    }
}

// ============================================================================
// ESP-IDF Entry Point
// ============================================================================

extern "C" void app_main(void)
{
    // Call setup once
    setup();

    // Run main loop forever
    while (1)
    {
        loop();
    }
}
