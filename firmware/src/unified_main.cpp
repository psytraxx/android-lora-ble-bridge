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
#include "common/Logging.h"
#include "common/Protocol.h"
#include "common/ApplicationController.h"
#include "common/LoRaManager.h"
#include "common/MessageQueue.h"
#include "common/FirmwareConfig.h"
#include "common/LEDManager.h"

static const char *TAG = "Main";

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

// Message queues
static MessageQueue bleToLoraQueue;
static MessageQueue loraToBleQueue;

#ifdef LED_PIN
LEDManager *ledManager = new LEDManager(LED_PIN);
#endif

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

    LOG_I(TAG, "\n\n=== LoRa-BLE Bridge (Trait-Based) ===");
    LOG_I(TAG, "Platform: %s", PLATFORM_NAME);
    LOG_I(TAG, "Device: %s", DEVICE_NAME);

    // Platform-specific initialization
    Platform::initializeWatchdog();
    Platform::initializePower();

#ifdef LED_PIN
    ledManager->setup();
#endif

    // Initialize application controller (static allocation)
    appController = &appControllerInstance;
    appController->begin();

    // Initialize storage manager (static allocation)
    storageManager = &storageManagerInstance;
    if (!storageManager->begin())
    {
        LOG_I(TAG, "Storage initialization failed!");
    }

    // Initialize BLE manager (heap allocation required due to different constructors)
    // Both platforms now use queue-based message handling
    bleManager = new typename Platform::BLEManager(&bleToLoraQueue);

    if (!bleManager->setup(DEVICE_NAME))
    {
        LOG_I(TAG, "BLE initialization failed!");
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
        LOG_I(TAG, "LoRa initialization failed!");
        while (1)
            ;
    }

    loraManager->setReceiveCallback(onLoRaReceived);
    loraManager->setTransmitCallback(onLoRaTransmitted);

    if (!loraManager->startReceive())
    {
        LOG_I(TAG, "Failed to start LoRa receive mode!");
    }
#ifdef LED_PIN
    ledManager->setOff();
#endif
    LOG_I(TAG, "Setup complete!");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop()
{
    // Reset watchdog
    Platform::resetWatchdog();

// Update LED state machine (non-blocking)
#ifdef LED_PIN
    ledManager->update();
#endif

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
                    LOG_I(TAG, "Failed to send message to BLE");
                }
            }
            else
            {
                LOG_I(TAG, "BLE not connected, buffering message");
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
                LOG_I(TAG, "Sent buffered message, %d remaining", storageManager->getCount());
                appController->notifyActivity();
            }
            else
            {
                LOG_I(TAG, "Failed to send buffered message (notify failed), will retry");
            }
        }
    }

    // Battery monitoring (rollover-safe comparison)
    unsigned long now = millis();
    unsigned long batteryElapsed = (unsigned long)(now - lastBatteryUpdate);
    if (batteryElapsed >= BATTERY_UPDATE_INTERVAL)
    {
        uint8_t batteryLevel = Platform::readBatteryLevel();

        bleManager->updateBatteryLevel(batteryLevel);

        LOG_D(TAG, "Battery: %d%%", batteryLevel);

        lastBatteryUpdate = now;
    }

    // Inactivity timeout - enter deep sleep to save power
    unsigned long inactiveTime = appController->getInactivityDuration();

    if (inactiveTime > PowerConstants::INACTIVITY_TIMEOUT_MS)
    {
        LOG_I(TAG, "Inactivity timeout - entering deep sleep...");

#ifdef LED_PIN
        ledManager->setOff();
#endif
        if (!loraManager->startReceive(true))
        {
            LOG_I(TAG, "Failed to start LoRa continuous receive mode!");
        }
        bleManager->stopAdvertising();

        Platform::sleep();

        // Should not reach here (device resets on wake)
    }

    delay(10);
}

// ============================================================================
// Callbacks
// ============================================================================

void onBleConnected()
{
    LOG_I(TAG, "BLE connected");
    appController->onBleConnected();
}

void onBleDisconnected()
{
    LOG_I(TAG, "BLE disconnected");
    appController->onBleDisconnected();

    // Restart advertising for next connection
    bleManager->startAdvertising();
}

void onLoRaReceived(const LoRaPacket &packet)
{
    LOG_I(TAG, "LoRa packet received: %d bytes, RSSI: %d dBm, SNR: %.1f dB",
          packet.len, packet.rssi, packet.snr);

    Message msg;
    if (msg.deserialize(packet.buffer, packet.len))
    {
        LOG_D(TAG, "Message type: %d", (int)msg.type);

        // Send ACK for Text messages
        if (msg.type == MessageType::Text)
        {
            LOG_D(TAG, "Sending ACK for seq %d", msg.textData.seq);

            Message ackMsg = Message::createAck(msg.textData.seq);
            uint8_t ackBuffer[64];
            int ackLen = ackMsg.serialize(ackBuffer, sizeof(ackBuffer));

            if (ackLen > 0)
            {
                // Wait for sender to switch to RX mode before sending ACK
                // This delay ensures reliable ACK delivery
                delay(LoRaConstants::ACK_DELAY_MS);

                // Send ACK (will be queued after current RX processing completes)
                if (loraManager->startTransmit(ackBuffer, (size_t)ackLen))
                {
                    LOG_I(TAG, "ACK transmission started");
                }
                else
                {
                    LOG_I(TAG, "Failed to start ACK transmission");
                }
            }
            else
            {
                LOG_I(TAG, "Failed to serialize ACK");
            }
        }

        if (loraToBleQueue.push(msg))
        {
            appController->notifyActivity();
        }
        else
        {
            LOG_I(TAG, "LoRa->BLE queue full!");
        }
    }
    else
    {
        LOG_I(TAG, "Failed to deserialize LoRa message");
    }
#ifdef LED_PIN
    ledManager->blink(LEDConstants::RX_BLINKS);
#endif
}

void onLoRaTransmitted(bool success)
{
    if (success)
    {
        LOG_I(TAG, "LoRa transmission successful");
#ifdef LED_PIN
        ledManager->blink(LEDConstants::TX_BLINKS);
#endif
    }
    else
    {
        LOG_I(TAG, "LoRa transmission failed");
    }
}

void handleBleMessage(const Message &msg)
{
    LOG_D(TAG, "BLE message queued for LoRa, type: %d", (int)msg.type);

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
            LOG_I(TAG, "Failed to start LoRa transmission");
        }
    }
    else
    {
        LOG_I(TAG, "Failed to serialize message");
    }
}
