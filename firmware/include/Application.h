#ifndef APPLICATION_H
#define APPLICATION_H

#include <Arduino.h>
#include "Protocol.h"
#include "common/MessageQueue.h"

// Forward declare LoRaConfig and LoRaPacket (defined in platform LoRaManager.h)
#ifndef LORA_CONFIG_DEFINED
#define LORA_CONFIG_DEFINED
struct LoRaConfig
{
    float frequency;
    float bandwidth;
    uint8_t spreadingFactor;
    uint8_t codingRate;
    int8_t txPower;
};

struct LoRaPacket
{
    uint8_t buffer[256];
    int len;
    int rssi;
    float snr;
};
#endif

/**
 * @brief Template-based Application (Compile-Time Polymorphism)
 *
 * This template class contains all business logic for the LoRa-BLE bridge.
 * Platform-specific behavior is injected via template parameters.
 *
 * Template Parameters:
 * - BLEManager: Platform-specific BLE implementation
 * - LoRaManager: Platform-specific LoRa implementation
 * - StorageManager: Platform-specific storage implementation
 * - PowerManager: Platform-specific power management
 * - SystemManager: Platform-specific system utilities
 * - ActivityManager: Platform-specific activity tracking
 *
 * Benefits of template approach:
 * - Zero runtime overhead (no virtual functions)
 * - Compile-time type checking
 * - Aggressive compiler optimizations (inlining)
 * - Simpler than port/adapter pattern
 */
template <typename BLEManager,
          typename LoRaManager,
          typename StorageManager,
          typename PowerManager,
          typename SystemManager,
          typename ActivityManager>
class Application
{
public:
    /**
     * @brief Construct application with managers
     *
     * Managers are passed by value and moved into the application.
     * This allows platform-specific initialization before construction.
     */
    Application(BLEManager &&ble,
                LoRaManager &&lora,
                StorageManager &&storage,
                PowerManager &&power,
                SystemManager &&system,
                ActivityManager &&activity)
        : bleManager(std::move(ble)),
          loraManager(std::move(lora)),
          storageManager(std::move(storage)),
          powerManager(std::move(power)),
          systemManager(std::move(system)),
          activityManager(std::move(activity)),
          lastBatteryUpdate(0) {}

    /**
     * @brief Initialize the application
     *
     * Sets up all managers and starts advertising/receiving.
     */
    void setup(const char *deviceName, const LoRaConfig &loraConfig);

    /**
     * @brief Main application loop
     *
     * Processes messages, monitors battery, handles timeouts.
     */
    void loop();

private:
    // Managers (platform-specific implementations)
    BLEManager bleManager;
    LoRaManager loraManager;
    StorageManager storageManager;
    PowerManager powerManager;
    SystemManager systemManager;
    ActivityManager activityManager;

    // Message queues
    MessageQueue bleToLoraQueue;
    MessageQueue loraToBleQueue;

    // Timing
    unsigned long lastBatteryUpdate;
    static constexpr unsigned long BATTERY_UPDATE_INTERVAL = 60000; // 1 minute

    // Callback handlers (template members can access private data)
    void onBleConnected();
    void onBleDisconnected();
    void onLoRaReceived(const LoRaPacket &packet);
    void onLoRaTransmitted(bool success);
    void handleBleMessage(const Message &msg);
};

// ============================================================================
// Implementation (must be in header for templates)
// ============================================================================

template <typename BLE, typename LoRa, typename Storage, typename Power, typename System, typename Activity>
void Application<BLE, LoRa, Storage, Power, System, Activity>::setup(
    const char *deviceName, const LoRaConfig &loraConfig)
{
    // Initialize Serial
    Serial.begin(115200);
    while (!Serial && systemManager.millis() < 3000)
        ; // Wait up to 3s
    systemManager.delay(500);

    Serial.println("\n\n=== LoRa-BLE Bridge (Template-Based) ===");
    Serial.print("Device: ");
    Serial.println(deviceName);

    // Initialize LED
    systemManager.ledOn();

    // Initialize storage
    if (!storageManager.begin())
    {
        Serial.println("Storage initialization failed!");
    }

    // Initialize power management
    if (!powerManager.begin())
    {
        Serial.println("Power management initialization failed!");
    }

    // Initialize BLE - pass callback lambdas that capture 'this'
    if (!bleManager.setup(deviceName))
    {
        Serial.println("BLE initialization failed!");
        while (1)
            ;
    }

    bleManager.setConnectionCallbacks(
        [this]() { this->onBleConnected(); },
        [this]() { this->onBleDisconnected(); });

    bleManager.setMessageCallback(
        [this](const Message &msg) { this->handleBleMessage(msg); });

    bleManager.startAdvertising();

    // Initialize LoRa
    if (!loraManager.begin(loraConfig))
    {
        Serial.println("LoRa initialization failed!");
        while (1)
            ;
    }

    loraManager.setReceiveCallback(
        [this](const LoRaPacket &packet) { this->onLoRaReceived(packet); });

    loraManager.setTransmitCallback(
        [this](bool success) { this->onLoRaTransmitted(success); });

    if (!loraManager.startReceive(true))
    {
        Serial.println("Failed to start LoRa receive mode!");
    }

    systemManager.ledOff();
    Serial.println("Setup complete!");
}

template <typename BLE, typename LoRa, typename Storage, typename Power, typename System, typename Activity>
void Application<BLE, LoRa, Storage, Power, System, Activity>::loop()
{
    // Reset watchdog
    systemManager.watchdogReset();

    // Process BLE incoming messages (no-op for ESP32, polls queue for nRF52)
    bleManager.processIncomingMessages();

    // Process LoRa events
    loraManager.process();

    // Forward LoRa → BLE
    Message msg;
    if (!loraToBleQueue.isEmpty())
    {
        if (loraToBleQueue.pop(msg))
        {
            if (bleManager.isConnected())
            {
                if (bleManager.sendMessage(msg))
                {
                    activityManager.markActivity();
                }
                else
                {
                    Serial.println("Failed to send message to BLE");
                }
            }
            else
            {
                Serial.println("BLE not connected, buffering message");
                storageManager.add(msg);
            }
        }
    }

    // Send buffered messages
    if (bleManager.isConnected() && !storageManager.isEmpty())
    {
        Message bufferedMsg;
        if (storageManager.peek(bufferedMsg))
        {
            if (bleManager.sendMessage(bufferedMsg))
            {
                storageManager.popFront();
                Serial.print("Sent buffered message, ");
                Serial.print(storageManager.getCount());
                Serial.println(" remaining");
                activityManager.markActivity();
            }
        }
    }

    // Forward BLE → LoRa
    if (!bleToLoraQueue.isEmpty())
    {
        if (bleToLoraQueue.pop(msg))
        {
            Serial.println("Transmitting BLE message via LoRa");

            uint8_t buffer[256];
            int length = msg.serialize(buffer, sizeof(buffer));

            if (length > 0)
            {
                if (loraManager.startTransmit(buffer, (size_t)length))
                {
                    activityManager.markActivity();
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

    // Battery monitoring
    unsigned long now = systemManager.millis();
    if (now - lastBatteryUpdate >= BATTERY_UPDATE_INTERVAL)
    {
        uint8_t batteryLevel = powerManager.readBatteryLevel();
        bleManager.updateBatteryLevel(batteryLevel);

        Serial.print("Battery: ");
        Serial.print(batteryLevel);
        Serial.println("%");

        lastBatteryUpdate = now;
    }

    // Inactivity timeout
    if (activityManager.isBleConnected())
    {
        unsigned long inactiveTime = activityManager.getInactivityDuration();
        if (inactiveTime > activityManager.getInactivityTimeout())
        {
            Serial.println("Inactivity timeout - disconnecting BLE");
            bleManager.disconnect();
        }
    }

    systemManager.delay(10);
}

// ============================================================================
// Callbacks
// ============================================================================

template <typename BLE, typename LoRa, typename Storage, typename Power, typename System, typename Activity>
void Application<BLE, LoRa, Storage, Power, System, Activity>::onBleConnected()
{
    Serial.println("BLE connected");
    activityManager.onBleConnected();
    activityManager.markActivity();
    systemManager.ledOn();
}

template <typename BLE, typename LoRa, typename Storage, typename Power, typename System, typename Activity>
void Application<BLE, LoRa, Storage, Power, System, Activity>::onBleDisconnected()
{
    Serial.println("BLE disconnected");
    activityManager.onBleDisconnected();
    systemManager.ledOff();
}

template <typename BLE, typename LoRa, typename Storage, typename Power, typename System, typename Activity>
void Application<BLE, LoRa, Storage, Power, System, Activity>::onLoRaReceived(const LoRaPacket &packet)
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

        if (loraToBleQueue.push(msg))
        {
            activityManager.markActivity();
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

    systemManager.ledBlink(systemManager.getLedRxBlinks());
}

template <typename BLE, typename LoRa, typename Storage, typename Power, typename System, typename Activity>
void Application<BLE, LoRa, Storage, Power, System, Activity>::onLoRaTransmitted(bool success)
{
    if (success)
    {
        Serial.println("LoRa transmission successful");
        systemManager.ledBlink(systemManager.getLedTxBlinks());
    }
    else
    {
        Serial.println("LoRa transmission failed");
    }
}

template <typename BLE, typename LoRa, typename Storage, typename Power, typename System, typename Activity>
void Application<BLE, LoRa, Storage, Power, System, Activity>::handleBleMessage(const Message &msg)
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

#endif // APPLICATION_H
