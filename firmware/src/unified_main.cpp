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

// Use pointers to allow conditional initialization, but allocate statically
static typename Platform::BLEManager *bleManager = nullptr;
static typename Platform::StorageManager *storageManager = nullptr;

// Static storage for manager instances (avoids heap allocation)
static typename Platform::StorageManager storageManagerInstance;

// Message queues
static MessageQueue bleToLoraQueue;
static MessageQueue loraToBleQueue;

// LoRa manager instance
static LoRaManager *loraManager = nullptr;

#ifdef LED_PIN
static LEDManager *ledManager = new LEDManager(LED_PIN, LEDConstants::HEARTBEAT_INTERVAL_MS, LEDConstants::HEARTBEAT_DURATION_MS);
#endif

// Battery monitoring timer
static TimerHandle_t batteryTimerHandle = nullptr;

// Deep sleep inactivity timer
static TimerHandle_t deepSleepTimerHandle = nullptr;

// ============================================================================
// Forward Declarations
// ============================================================================

void onBleConnected();
void onBleDisconnected();
void onLoRaReceived(const LoRaPacket &packet);
void onLoRaTransmitted(bool success);
void handleBleMessage(const Message &msg);

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Reset inactivity timer on activity (BLE or LoRa events)
 *
 * This function resets the deep sleep timer whenever there's device activity.
 * Should be called when BLE messages are received/sent or LoRa packets arrive.
 */
static inline void resetInactivityTimer()
{
    if (deepSleepTimerHandle != nullptr)
    {
        // Reset timer from any context (ISR-safe version available if needed)
        xTimerReset(deepSleepTimerHandle, 0);
    }
}

// ============================================================================
// Battery Timer Callback
// ============================================================================

/**
 * @brief FreeRTOS timer callback for periodic battery level updates
 *
 * This callback runs periodically (every 60 seconds) from the timer task context.
 * It reads the battery level and updates the BLE battery characteristic.
 *
 * @param xTimer Handle of the timer that triggered this callback
 */
static void batteryTimerCallback(TimerHandle_t xTimer)
{
    (void)xTimer; // Unused parameter

    uint8_t batteryLevel = Platform::readBatteryLevel();

    if (bleManager != nullptr)
    {
        bleManager->updateBatteryLevel(batteryLevel);
    }

    LOG_D(TAG, "Battery: %d%%", batteryLevel);
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
    LOG_I(TAG, "Device: %s", DEVICE_NAME);

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

    if (!loraManager->begin())
    {
        LOG_I(TAG, "LoRa initialization failed!");
        while (1)
            ;
    }

    loraManager->setReceiveCallback(onLoRaReceived);
    loraManager->setTransmitCallback(onLoRaTransmitted);

    // Start receive in duty cycle mode (power saving) where supported
    if (!loraManager->startReceive(true))
    {
        LOG_I(TAG, "Failed to start LoRa receive mode!");
    }

    // Create and start battery monitoring timer (auto-reload, 60 second period)
    batteryTimerHandle = xTimerCreate(
        "BatteryTimer",                                      // Timer name (for debugging)
        pdMS_TO_TICKS(BatteryConstants::UPDATE_INTERVAL_MS), // Period in ticks (60 seconds)
        pdTRUE,                                              // Auto-reload timer
        (void *)0,                                           // Timer ID (not used)
        batteryTimerCallback                                 // Callback function
    );

    if (batteryTimerHandle != nullptr)
    {
        if (xTimerStart(batteryTimerHandle, 0) == pdPASS)
        {
            LOG_I(TAG, "Battery monitoring timer started (interval: %lu ms)", BatteryConstants::UPDATE_INTERVAL_MS);
        }
        else
        {
            LOG_E(TAG, "Failed to start battery monitoring timer!");
        }
    }
    else
    {
        LOG_E(TAG, "Failed to create battery monitoring timer!");
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
                LOG_I(TAG, "BLE not connected, buffering message");
                storageManager->add(msg);
            }
        }
    }

    // Send buffered messages only when client is connected and has enabled notifications
    if (bleManager->isConnected() && bleManager->areNotificationsEnabled() && !storageManager->isEmpty())
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

    // Wait for Android GATT stack to complete setup (500ms is sufficient)
    delay(500);
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
                // Wait for sender to switch to RX mode before sending ACK
                // Use actual packet length for accurate timing calculation
                // Random jitter prevents multiple receivers from ACKing simultaneously
                int ackDelay = LoRaConstants::getAckDelay(
                    LoRaConstants::SPREADING_FACTOR,
                    LoRaConstants::BANDWIDTH,
                    LoRaConstants::CODING_RATE,
                    LoRaConstants::PREAMBLE_LENGTH,
                    packet.len);
                LOG_D(TAG, "ACK delay: %d ms (includes jitter, based on %d byte packet)", ackDelay, packet.len);
                delay(ackDelay);

                // Send ACK (will be queued after current RX processing completes)
                if (loraManager->startTransmit(ackBuffer, (size_t)ackLen, false))
                {
                    LOG_I(TAG, "ACK transmission started for seq %d", msg.textData.seq);
                }
                else
                {
                    LOG_W(TAG, "Failed to start ACK transmission");
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
        if (loraManager->startTransmit(msgBuffer, (size_t)msgLen))
        {
            resetInactivityTimer();
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
