//! Unified Firmware for LoRa-BLE Bridge (Trait-Based Architecture)
//!
//! This single main.cpp works on both ESP32 and nRF52 by using platform traits.
//! Platform-specific behavior is selected at compile-time via PlatformTraits.
//!
//! Architecture:
//! - Single main.cpp for all platforms
//! - Platform traits provide types, constants, and platform-specific operations
//! - No virtual functions, no runtime overhead
//! - Clean separation of platform-specific vs common code

#include <Arduino.h>
#include "common/Protocol.h"
#include "common/ApplicationController.h"
#include "common/LoRaManager.h"
#include "common/MessageQueue.h"

// Select platform traits based on build target
#if defined(ARDUINO_ARCH_ESP32)
#include "esp32/PlatformTraits.h"
using Platform = ESP32PlatformTraits;
#define PLATFORM_NAME "ESP32"
#elif defined(ARDUINO_ARCH_NRF52)
#include "nrf52/PlatformTraits.h"
using Platform = NRF52PlatformTraits;
#define PLATFORM_NAME "nRF52"
#else
#error "Unsupported platform"
#endif

// ============================================================================
// Global Managers (using platform-specific types)
// ============================================================================

// Use pointers to allow conditional initialization, but allocate statically
static typename Platform::BLEManager *bleManager = nullptr;
static LoRaManager *loraManager = nullptr;
static typename Platform::StorageManager *storageManager = nullptr;

// Static storage for manager instances (avoids heap allocation)
static typename Platform::StorageManager storageManagerInstance;

// Application controller (activity tracking and state management)
static ApplicationController *appController = nullptr;
static ApplicationController appControllerInstance;

// nRF52 needs PowerManager instance, ESP32 uses static methods
#if defined(ARDUINO_ARCH_NRF52)
static typename Platform::PowerManager *powerManager = nullptr;
static typename Platform::PowerManager powerManagerInstance;
#endif

// Message queues
static MessageQueue bleToLoraQueue;
static MessageQueue loraToBleQueue;

// Timing
static unsigned long lastBatteryUpdate = 0;
static constexpr unsigned long BATTERY_UPDATE_INTERVAL = 60000; // 1 minute

// ============================================================================
// Forward Declarations
// ============================================================================

void onBleConnected();
void onBleDisconnected();
void onLoRaReceived(const LoRaPacket &packet);
void onLoRaTransmitted(bool success);
void handleBleMessage(const Message &msg);

// ============================================================================
// Setup
// ============================================================================

void setup()
{
    // Initialize Serial
    Serial.begin(115200);
    while (!Serial && millis() < 3000)
        ;
    delay(500);

    Serial.println("\n\n=== LoRa-BLE Bridge (Trait-Based) ===");
    Serial.print("Platform: ");
    Serial.println(PLATFORM_NAME);
    Serial.print("Device: ");
    Serial.println(DEVICE_NAME);

    // Platform-specific initialization
    Platform::initializeWatchdog();
    Platform::initializePower();
    Platform::initializeLED();
    Platform::ledOn();

    // Initialize application controller (static allocation)
    appController = &appControllerInstance;
    appController->begin();

    // Initialize storage manager (static allocation)
    storageManager = &storageManagerInstance;
    if (!storageManager->begin())
    {
        Serial.println("Storage initialization failed!");
    }

    // Initialize power manager (nRF52 only, static allocation)
#if defined(ARDUINO_ARCH_NRF52)
    powerManager = &powerManagerInstance;
    if (!powerManager->begin())
    {
        Serial.println("Power manager initialization failed!");
    }
#endif

    // Initialize BLE manager (heap allocation required due to different constructors)
    // Both platforms now use queue-based message handling
    bleManager = new typename Platform::BLEManager(&bleToLoraQueue);

    if (!bleManager->setup(DEVICE_NAME))
    {
        Serial.println("BLE initialization failed!");
        while (1)
            ;
    }
    bleManager->setConnectionCallbacks(onBleConnected, onBleDisconnected);
    bleManager->startAdvertising();

    // Initialize LoRa manager (heap allocation required due to runtime pin configuration)
    // TODO: Consider static allocation with placement new if memory is constrained
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

    if (!loraManager->startReceive())
    {
        Serial.println("Failed to start LoRa receive mode!");
    }

    Platform::ledOff();
    Serial.println("Setup complete!");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop()
{
    // Reset watchdog
    Platform::resetWatchdog();

    // Update LED state machine (non-blocking)
    Platform::updateLED();

    // Process LoRa events
    loraManager->process();

    // Message variable for queue operations
    Message msg;

    // Process BLE incoming messages (both platforms use queue-based approach)
    while (bleToLoraQueue.pop(msg))
    {
        handleBleMessage(msg);
    }

    // Forward LoRa → BLE
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
                Serial.println("BLE not connected, buffering message");
                storageManager->add(msg);
            }
        }
    }

    // Send buffered messages only when client is connected and has enabled notifications
    if (bleManager->isConnected() && bleManager->areNotificationsEnabled() && appController->isAndroidReady() && !storageManager->isEmpty())
    {
        Message bufferedMsg;
        if (storageManager->peek(bufferedMsg))
        {
            if (bleManager->sendMessage(bufferedMsg))
            {
                storageManager->popFront();
                Serial.print("Sent buffered message, ");
                Serial.print(storageManager->getCount());
                Serial.println(" remaining");
                appController->notifyActivity();
            }
            else
            {
                Serial.println("Failed to send buffered message (notify failed), will retry");
            }
        }
    }

    // Battery monitoring (rollover-safe comparison)
    unsigned long now = millis();
    unsigned long batteryElapsed = (unsigned long)(now - lastBatteryUpdate);
    if (batteryElapsed >= BATTERY_UPDATE_INTERVAL)
    {
#if defined(ARDUINO_ARCH_ESP32)
        uint8_t batteryLevel = Platform::readBatteryLevel();
#else
        uint8_t batteryLevel = Platform::readBatteryLevel(*powerManager);
#endif
        bleManager->updateBatteryLevel(batteryLevel);

        Serial.print("Battery: ");
        Serial.print(batteryLevel);
        Serial.println("%");

        lastBatteryUpdate = now;
    }

    // Inactivity timeout - enter deep sleep to save power
    unsigned long inactiveTime = appController->getInactivityDuration();

    if (inactiveTime > Platform::INACTIVITY_TIMEOUT_MS)
    {
        Serial.println("Inactivity timeout - entering deep sleep...");
        Serial.flush();
        delay(100); // Allow serial output to complete

        Platform::ledOff();

        if (!loraManager->startReceive(true))
        {
            Serial.println("Failed to start LoRa continuous receive mode!");
        }
        bleManager->stopAdvertising();

#if defined(ARDUINO_ARCH_ESP32)
        PowerManager::enterDeepSleep();
#elif defined(ARDUINO_ARCH_NRF52)
        powerManager->enterLowPowerMode();
#endif

        // Should not reach here (device resets on wake)
    }

    delay(10);
}

// ============================================================================
// Callbacks
// ============================================================================

void onBleConnected()
{
    Serial.println("BLE connected");
    appController->onBleConnected();
    Platform::ledOn();
}

void onBleDisconnected()
{
    Serial.println("BLE disconnected");
    appController->onBleDisconnected();
    Platform::ledOff();

    // Restart advertising for next connection
    bleManager->startAdvertising();
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

    Message msg;
    if (msg.deserialize(packet.buffer, packet.len))
    {
        Serial.print("Message type: ");
        Serial.println((int)msg.type);

        // Send ACK for Text messages
        if (msg.type == MessageType::Text)
        {
            Serial.print("Sending ACK for seq ");
            Serial.println(msg.textData.seq);

            Message ackMsg = Message::createAck(msg.textData.seq);
            uint8_t ackBuffer[64];
            int ackLen = ackMsg.serialize(ackBuffer, sizeof(ackBuffer));

            if (ackLen > 0)
            {
                // Send ACK (will be queued after current RX processing completes)
                if (loraManager->startTransmit(ackBuffer, (size_t)ackLen))
                {
                    Serial.println("ACK transmission started");
                }
                else
                {
                    Serial.println("Failed to start ACK transmission");
                }
            }
            else
            {
                Serial.println("Failed to serialize ACK");
            }
        }

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

    Platform::ledBlink(Platform::LED_RX_BLINKS);
}

void onLoRaTransmitted(bool success)
{
    if (success)
    {
        Serial.println("LoRa transmission successful");
        Platform::ledBlink(Platform::LED_TX_BLINKS);
    }
    else
    {
        Serial.println("LoRa transmission failed");
    }
}

void handleBleMessage(const Message &msg)
{
    Serial.print("BLE message queued for LoRa, type: ");
    Serial.println((int)msg.type);

    uint8_t buffer[256];
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
