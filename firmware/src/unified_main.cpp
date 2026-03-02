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
#include <memory>
#include "common/Logging.h"
#include "common/Protocol.h"
#include "common/LoRaManager.h"
#include "common/MessageQueue.h"
#include "common/FirmwareConfig.h"
#include "common/LEDManager.h"

// Platform-specific FreeRTOS includes
#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#elif defined(ARDUINO_ARCH_NRF52)
#include <FreeRTOS.h>
#include <timers.h>
#endif

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

// Use unique_ptr for heap-allocated managers (clarifies ownership, zero runtime cost)
static std::unique_ptr<typename Platform::BLEManager> bleManager;
static typename Platform::StorageManager *storageManager = nullptr;

// Static storage for manager instances (avoids heap allocation)
static typename Platform::StorageManager storageManagerInstance;

// Message queues
static MessageQueue bleToLoraQueue;
static MessageQueue loraToBleQueue;

// LoRa manager instance
static std::unique_ptr<LoRaManager> loraManager;

#ifdef LED_PIN
static std::unique_ptr<LEDManager> ledManager(new LEDManager(LED_PIN, LEDConstants::HEARTBEAT_INTERVAL_MS, LEDConstants::HEARTBEAT_DURATION_MS));
#endif

// Deep sleep inactivity timer
static TimerHandle_t deepSleepTimerHandle = nullptr;

// Deadline after which the BLE GATT stack is considered ready for use.
// Avoids blocking the main loop after connection (replaces delay(500) in callback).
static uint32_t bleGattReadyAt = 0;

// Last received LoRa signal quality (updated on every LoRa receive)
static int lastRssi = 0;
static float lastSnr = 0.0f;

// ============================================================================
// Forward Declarations
// ============================================================================

void onBleConnected();
void onBleDisconnected();
void onLoRaReceived(const LoRaPacket &packet);
void onLoRaTransmitted(bool success);
void handleBleMessage(const Message &msg);
DeviceInfoData provideDeviceInfo();

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Reset inactivity timer on activity (BLE or LoRa events)
 */
static inline void resetInactivityTimer()
{
    if (deepSleepTimerHandle != nullptr)
    {
        xTimerReset(deepSleepTimerHandle, 0);
    }
}

/**
 * @brief Device info provider callback for BLEManager
 *
 * Called on-demand when the client reads the device info characteristic.
 * Returns fresh battery level, stored RSSI/SNR, and compile-time LoRa config.
 */
DeviceInfoData provideDeviceInfo()
{
    DeviceInfoData info;
    info.batteryLevel = Platform::readBatteryLevel();
    info.rssi = static_cast<int16_t>(lastRssi);
    info.snrX100 = static_cast<int16_t>(lastSnr * 100);
    info.txPower = static_cast<int8_t>(LoRaConstants::TX_POWER);
    info.frequencyHz = static_cast<uint32_t>(LoRaConstants::FREQUENCY * 1000000);
    info.bandwidthHz = static_cast<uint32_t>(LoRaConstants::BANDWIDTH * 1000);
    info.spreadingFactor = LoRaConstants::SPREADING_FACTOR;
    info.codingRate = LoRaConstants::CODING_RATE;
    return info;
}

/**
 * @brief FreeRTOS timer callback for inactivity timeout (deep sleep trigger)
 *
 * This one-shot timer expires after the configured inactivity period.
 * When triggered, it prepares the device and enters deep sleep mode.
 * The timer is reset on any BLE or LoRa activity.
 *
 * @param xTimer Handle of the timer that triggered this callback
 */
static void deepSleepTimerCallback(TimerHandle_t xTimer)
{
    (void)xTimer; // Unused parameter

    LOG_I(TAG, "Inactivity timeout - entering deep sleep...");

    // Start LoRa in duty cycle mode before sleep
    if (loraManager != nullptr)
    {
        if (!loraManager->startReceive(true))
        {
            LOG_E(TAG, "Failed to start LoRa continuous receive mode!");
        }
    }

    // Stop BLE advertising
    if (bleManager != nullptr)
    {
        bleManager->stopAdvertising();
    }

    // Enter deep sleep (does not return)
    Platform::enterDeepSleep();

    // Should never reach here
    LOG_E(TAG, "Failed to enter deep sleep mode!");
}

// ============================================================================
// Setup
// ============================================================================

void setup()
{
    // Initialize Serial
    Serial.begin(115200);

    LOG_I(TAG, "\n\n=== LoRa-BLE Bridge (Trait-Based) ===");
    LOG_I(TAG, "Platform: %s", PLATFORM_NAME);
    LOG_I(TAG, "Base Device: %s", BASE_DEVICE_NAME);

    // Platform-specific initialization
    Platform::initializeWatchdog();
    Platform::initializePower();

#ifdef LED_PIN
    ledManager->setup();
#endif

    // Initialize storage manager (static allocation)
    storageManager = &storageManagerInstance;
    if (!storageManager->begin())
    {
        LOG_I(TAG, "Storage initialization failed!");
    }

    // Construct device name with MAC suffix
    char deviceName[32];
    String macSuffix = Platform::getMacSuffix();
    snprintf(deviceName, sizeof(deviceName), "%s-%s", BASE_DEVICE_NAME, macSuffix.c_str());
    LOG_I(TAG, "Device: %s", deviceName);

    // Initialize BLE manager (heap allocation required due to different constructors)
    // Both platforms now use queue-based message handling
    bleManager = std::unique_ptr<typename Platform::BLEManager>(new typename Platform::BLEManager(&bleToLoraQueue));

    if (!bleManager->setup(deviceName))
    {
        LOG_I(TAG, "BLE initialization failed!");
        while (1)
            ;
    }
    bleManager->setConnectionCallbacks(onBleConnected, onBleDisconnected);
    bleManager->setInfoDataProvider(provideDeviceInfo);
    bleManager->startAdvertising();

    // Initialize LoRa manager (heap allocation required due to runtime pin configuration)
    loraManager = std::unique_ptr<LoRaManager>(new LoRaManager(
        LORA_SCK,
        LORA_MISO,
        LORA_MOSI,
        LORA_SS,
        LORA_RST,
        LORA_DIO0,
        LORA_BUSY));

    loraManager->setReceiveCallback(onLoRaReceived);
    loraManager->setTransmitCallback(onLoRaTransmitted);

    bool initialized = false;

    if (Platform::isLoraWakeUp())
    {
        LOG_I(TAG, "Wakeup from LoRa detected, attempting to resume...");
        if (loraManager->handleSleepWakeup())
        {
            initialized = true;
            LOG_I(TAG, "LoRa resume successful");
        }
        else
        {
            LOG_W(TAG, "LoRa resume failed, falling back to full init");
        }
    }

    if (!initialized)
    {
        if (!loraManager->begin())
        {
            LOG_I(TAG, "LoRa initialization failed!");
            while (1)
                ;
        }
    }

    // Start receive in duty cycle mode (power saving) where supported
    // Only if not already transmitting (e.g. sending ACK from wakeup callback)
    if (!loraManager->isTransmitting())
    {
        if (!loraManager->startReceive(true))
        {
            LOG_I(TAG, "Failed to start LoRa receive mode!");
        }
    }

    // Create and start deep sleep inactivity timer (one-shot, resets on activity)
    deepSleepTimerHandle = xTimerCreate(
        "DeepSleepTimer",                                     // Timer name (for debugging)
        pdMS_TO_TICKS(PowerConstants::INACTIVITY_TIMEOUT_MS), // Period in ticks (60 seconds default)
        pdFALSE,                                              // One-shot timer (not auto-reload)
        (void *)0,                                            // Timer ID (not used)
        deepSleepTimerCallback                                // Callback function
    );

    if (deepSleepTimerHandle != nullptr)
    {
        if (xTimerStart(deepSleepTimerHandle, 0) == pdPASS)
        {
            LOG_I(TAG, "Deep sleep inactivity timer started (timeout: %lu ms)", PowerConstants::INACTIVITY_TIMEOUT_MS);
        }
        else
        {
            LOG_E(TAG, "Failed to start deep sleep timer!");
        }
    }
    else
    {
        LOG_E(TAG, "Failed to create deep sleep timer!");
    }

    LOG_I(TAG, "Setup complete!");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop()
{
    // Reset watchdog
    Platform::resetWatchdog();

// Update LED state machine (non-blocking, includes heartbeat)
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
                    resetInactivityTimer();
                }
                else
                {
                    LOG_I(TAG, "Failed to send message to BLE");
                }
            }
            else
            {
                // Only persist text messages for delivery to BLE client later.
                // ACKs and other protocol messages are not stored to avoid
                // unnecessary buffering of protocol-level traffic.
                if (msg.type == MessageType::Text)
                {
                    LOG_I(TAG, "BLE not connected, buffering text message");
                    storageManager->add(msg);
                }
                else
                {
                    LOG_I(TAG, "BLE not connected, dropping non-text message (type: %d)", (int)msg.type);
                }
            }
        }
    }

    // Send buffered messages only when client is connected, GATT stack has settled,
    // and notifications are enabled. The 500ms settle window after connection avoids
    // sending before Android completes MTU negotiation.
    if (bleManager->isConnected() && (uint32_t)millis() >= bleGattReadyAt &&
        bleManager->areNotificationsEnabled() && !storageManager->isEmpty())
    {
        Message bufferedMsg;
        if (storageManager->peek(bufferedMsg))
        {
            if (bleManager->sendMessage(bufferedMsg))
            {
                storageManager->popFront();
                LOG_I(TAG, "Sent buffered message, %d remaining", storageManager->getCount());
                resetInactivityTimer();
            }
            else
            {
                LOG_I(TAG, "Failed to send buffered message (notify failed), will retry");
            }
        }
    }

    delay(20);
}

// ============================================================================
// Callbacks
// ============================================================================

void onBleConnected()
{
    LOG_I(TAG, "BLE connected");
    resetInactivityTimer();

    // Record when the Android GATT stack will be ready for use.
    // Some Android BLE stacks need ~500ms after connection to complete MTU
    // negotiation. Rather than blocking here with delay(500), the main loop
    // defers buffered-message delivery until this deadline passes.
    bleGattReadyAt = millis() + 500;
}

void onBleDisconnected()
{
    LOG_I(TAG, "BLE disconnected");

    // Restart advertising for next connection
    bleManager->startAdvertising();
}

void onLoRaReceived(const LoRaPacket &packet)
{
    LOG_I(TAG, "LoRa packet received: %d bytes, RSSI: %d dBm, SNR: %.1f dB",
          packet.len, packet.rssi, packet.snr);

    // Store signal quality for device info characteristic
    lastRssi = packet.rssi;
    lastSnr = packet.snr;

    // Update BLE characteristic with fresh RSSI/SNR values
    bleManager->updateDeviceInfo();

#ifdef LED_PIN
    ledManager->blink(LEDConstants::RX_BLINKS);
#endif

    Message msg;
    if (msg.deserialize(packet.buffer, packet.len))
    {
        LOG_D(TAG, "Message type: %d", (int)msg.type);

        // Send ACK for Text messages
        if (msg.type == MessageType::Text)
        {
            LOG_D(TAG, "Sending ACK for seq %d", msg.textData.seq);

            Message ackMsg = Message::createAck(msg.textData.seq);
            uint8_t ackBuffer[BufferConstants::MAX_PROTOCOL_MESSAGE];
            int ackLen = ackMsg.serialize(ackBuffer, sizeof(ackBuffer));

            if (ackLen > 0)
            {
                // Enqueue ACK — CAD handles collision avoidance (no delay needed)
                if (loraManager->queueTransmit(ackBuffer, (size_t)ackLen))
                {
                    LOG_I(TAG, "ACK queued for seq %d", msg.textData.seq);
                }
                else
                {
                    LOG_W(TAG, "Failed to queue ACK transmission");
                }
            }
            else
            {
                LOG_I(TAG, "Failed to serialize ACK");
            }
        }

        if (loraToBleQueue.push(msg))
        {
            resetInactivityTimer();
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

    uint8_t msgBuffer[BufferConstants::MAX_PROTOCOL_MESSAGE];
    int msgLen = msg.serialize(msgBuffer, sizeof(msgBuffer));

    if (msgLen > 0)
    {
        if (loraManager->queueTransmit(msgBuffer, (size_t)msgLen))
        {
            resetInactivityTimer();
        }
        else
        {
            LOG_I(TAG, "Failed to queue LoRa transmission");
        }
    }
    else
    {
        LOG_I(TAG, "Failed to serialize message");
    }
}
