//! ESP32 Firmware for LoRa-BLE Bridge (Simplified Architecture - Arduino)
//!
//! This firmware implements a BLE peripheral that communicates with Android devices
//! and bridges BLE messages to LoRa transmission and reception.
//!
//! SIMPLIFIED ARCHITECTURE (matches nRF52):
//! - Single loop() - Arduino framework
//! - Event-driven: LoRaManager and BLEManager handle callbacks
//! - Simple message queues (no FreeRTOS queues needed)
//! - Same functionality, cleaner code
//!
//! Features:
//! - BLE communication with Android app
//! - LoRa radio TX/RX with interrupt handling
//! - Persistent message buffering (Preferences)
//! - Battery monitoring and power management
//! - Protocol-compatible with nRF52 firmware

#include <Arduino.h>
#include <Adafruit_SleepyDog.h>

#include "BLEManager.h"
#include "LoRaManager.h"
#include "Protocol.h"
#include "LEDManager.h"
#include "MessageBuffer.h"
#include "FirmwareConfig.h"
#include "ApplicationController.h"
#include "PowerManager.h"

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
    // Initialize Serial
    Serial.begin(115200);
    while (!Serial && millis() < 3000)
        ; // Wait up to 3s for Serial
    delay(500);

    Serial.println("\n\n=== ESP32 LoRa-BLE Bridge (Arduino) ===");
    Serial.print("Device: ");
    Serial.println(DEVICE_NAME);

    // Power management
    PowerManager::configurePowerManagement();

    // Initialize LED
#ifdef LED_PIN
    ledManager.setup();
    ledManager.setOn();
#endif

    // Configure watchdog (Arduino SleepyDog)
    int watchdogMS = Watchdog.enable(WatchdogConstants::TIMEOUT_SECONDS * 1000);
    Serial.print("Watchdog enabled: ");
    Serial.print(watchdogMS);
    Serial.println(" ms");

    // Initialize application controller
    appController = new ApplicationController();

    // Initialize message buffer (persistent storage)
    messageBuffer = new MessageBuffer();
    if (!messageBuffer->begin())
    {
        Serial.println("Message buffer initialization failed!");
    }

    // Initialize BLE manager
    bleManager = new BLEManager();
    if (!bleManager->setup(DEVICE_NAME))
    {
        Serial.println("BLE initialization failed!");
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
        Serial.println("LoRa initialization failed!");
        while (1)
            ;
    }

    loraManager->setReceiveCallback(onLoRaReceived);
    loraManager->setTransmitCallback(onLoRaTransmitted);

    // Start LoRa receive mode (duty cycle for power saving)
    if (!loraManager->startReceive(true))
    {
        Serial.println("Failed to start LoRa receive mode!");
    }

#ifdef LED_PIN
    ledManager.setOff();
#endif

    Serial.println("Setup complete!");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop()
{
    // Reset watchdog
    Watchdog.reset();

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
                    Serial.println("Failed to send message to BLE");
                }
            }
            else
            {
                // BLE not ready, buffer message for later
                Serial.println("BLE not connected, buffering message");
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
                Serial.print("Sent buffered message, ");
                Serial.print(messageBuffer->getCount());
                Serial.println(" remaining");
                appController->notifyActivity();
            }
        }
    }

    // Forward messages from BLE to LoRa
    if (!bleToLoraQueue.isEmpty())
    {
        if (bleToLoraQueue.pop(msg))
        {
            Serial.println("Transmitting BLE message via LoRa");

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
                    Serial.println("Failed to start LoRa transmission");
                }
            }
            else
            {
                Serial.println("Failed to serialize message");
            }
        }
    }

    // Update battery level periodically
    unsigned long now = millis();
    if (now - lastBatteryUpdate >= BATTERY_UPDATE_INTERVAL)
    {
        uint8_t batteryLevel = PowerManager::readBatteryLevel();
        bleManager->updateBatteryLevel(batteryLevel);

        Serial.print("Battery: ");
        Serial.print(batteryLevel);
        Serial.println("%");

        lastBatteryUpdate = now;
    }

    // Check for inactivity timeout
    if (bleManager->isConnected())
    {
        unsigned long inactiveTime = appController->getInactivityDuration();
        if (inactiveTime > PowerConstants::INACTIVITY_TIMEOUT_MS)
        {
            Serial.println("Inactivity timeout - disconnecting BLE");
            bleManager->disconnect();
        }
    }

    // Small delay to prevent busy-waiting
    delay(10);
}

// ============================================================================
// Callback Implementations
// ============================================================================

void onBleConnected()
{
    Serial.println("BLE connected");
    appController->onBleConnected();
    appController->notifyActivity();

#ifdef LED_PIN
    ledManager.setOn();
#endif
}

void onBleDisconnected()
{
    Serial.println("BLE disconnected");
    appController->onBleDisconnected();

#ifdef LED_PIN
    ledManager.setOff();
#endif
}

void onLoRaReceived(const LoRaPacket &packet)
{
    Serial.print("LoRa packet received: ");
    Serial.print(packet.len);
    Serial.print(" bytes, RSSI: ");
    Serial.print(packet.rssi);
    Serial.print(" dBm, SNR: ");
    Serial.print(packet.snr);
    Serial.println(" dB");

    // Deserialize message
    Message msg;
    if (msg.deserialize(packet.buffer, packet.len))
    {
        Serial.print("Message type: ");
        Serial.println((int)msg.type);

        // Forward to BLE
        if (loraToBleQueue.push(msg))
        {
            appController->notifyActivity();
        }
        else
        {
            Serial.println("LoRa->BLE queue full!");
        }
    }
    else
    {
        Serial.println("Failed to deserialize LoRa message");
    }

#ifdef LED_PIN
    ledManager.blink(LEDConstants::RX_BLINKS);
#endif
}

void onLoRaTransmitted(bool success)
{
    if (success)
    {
        Serial.println("LoRa transmission successful");

#ifdef LED_PIN
        ledManager.blink(LEDConstants::TX_BLINKS);
#endif
    }
    else
    {
        Serial.println("LoRa transmission failed");
    }
}

// ============================================================================
// BLE Message Handler (called by BLEManager)
// ============================================================================

void handleBleMessage(const Message &msg)
{
    if (!bleToLoraQueue.push(msg))
    {
        Serial.println("BLE->LoRa queue full");
    }
    else
    {
        Serial.print("BLE message queued for LoRa, type: ");
        Serial.println((int)msg.type);
    }
}
