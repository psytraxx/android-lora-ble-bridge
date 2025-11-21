//! Unified Firmware for LoRa-BLE Bridge (Hexagonal Architecture)
//!
//! This is a platform-agnostic main application that works on both ESP32 and nRF52.
//! Platform-specific code is isolated in adapters that implement port interfaces.
//!
//! Architecture:
//! - Application Core (this file): Business logic, message routing, state management
//! - Port Interfaces: Abstract interfaces for hardware/platform operations
//! - Platform Adapters: Concrete implementations for ESP32 and nRF52
//!
//! Features:
//! - BLE communication with Android app
//! - LoRa radio TX/RX with interrupt handling
//! - Persistent message buffering
//! - Battery monitoring and power management
//! - Inactivity timeout handling

#include <Arduino.h>
#include "ports/PlatformPorts.h"
#include "Protocol.h"
#include "common/MessageQueue.h"

// Get platform-specific LoRa configuration
extern LoRaConfig getPlatformLoRaConfig();
extern unsigned long getPowerInactivityTimeout();
extern int getLedRxBlinks();
extern int getLedTxBlinks();

// Global port interfaces (injected by platform)
static PlatformPorts ports;

// Message queues (in-memory circular buffers)
// Note: nRF52 needs access to bleToLoraQueue for BLEManager, so not static
MessageQueue bleToLoraQueue;
MessageQueue loraToBleQueue;

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
    // Get platform-specific adapters
    ports = createPlatformPorts();
    const char *deviceName = getDeviceName();

    // Initialize Serial
    Serial.begin(115200);
    while (!Serial && ports.system->millis() < 3000)
        ; // Wait up to 3s for Serial
    ports.system->delay(500);

    Serial.println("\n\n=== LoRa-BLE Bridge (Unified) ===");
    Serial.print("Device: ");
    Serial.println(deviceName);

    // Initialize LED
    ports.system->ledOn();

    // Initialize storage (persistent message buffer)
    if (!ports.storage->begin())
    {
        Serial.println("Storage initialization failed!");
    }

    // Initialize power management
    if (!ports.power->begin())
    {
        Serial.println("Power management initialization failed!");
    }

    // Initialize BLE manager
    if (!ports.ble->setup(deviceName))
    {
        Serial.println("BLE initialization failed!");
        while (1)
            ;
    }
    ports.ble->setConnectionCallbacks(onBleConnected, onBleDisconnected);
    ports.ble->setMessageCallback(handleBleMessage);
    ports.ble->startAdvertising();

    // Initialize LoRa manager
    LoRaConfig loraConfig = getPlatformLoRaConfig();

    if (!ports.lora->begin(loraConfig))
    {
        Serial.println("LoRa initialization failed!");
        while (1)
            ;
    }

    ports.lora->setReceiveCallback(onLoRaReceived);
    ports.lora->setTransmitCallback(onLoRaTransmitted);

    // Start LoRa receive mode (duty cycle for power saving)
    if (!ports.lora->startReceive(true))
    {
        Serial.println("Failed to start LoRa receive mode!");
    }

    ports.system->ledOff();
    Serial.println("Setup complete!");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop()
{
    // Reset watchdog
    ports.system->watchdogReset();

    // Process LoRa events (RX/TX completion)
    ports.lora->process();

    // Forward messages from LoRa to BLE
    Message msg;
    if (!loraToBleQueue.isEmpty())
    {
        if (loraToBleQueue.pop(msg))
        {
            if (ports.ble->isConnected())
            {
                if (ports.ble->sendMessage(msg))
                {
                    ports.activity->markActivity();
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
                ports.storage->add(msg);
            }
        }
    }

    // Send buffered messages when BLE becomes ready
    if (ports.ble->isConnected() && !ports.storage->isEmpty())
    {
        Message bufferedMsg;
        if (ports.storage->peek(bufferedMsg))
        {
            if (ports.ble->sendMessage(bufferedMsg))
            {
                ports.storage->popFront();
                Serial.print("Sent buffered message, ");
                Serial.print(ports.storage->getCount());
                Serial.println(" remaining");
                ports.activity->markActivity();
            }
        }
    }

    // Forward messages from BLE to LoRa
    if (!bleToLoraQueue.isEmpty())
    {
        if (bleToLoraQueue.pop(msg))
        {
            Serial.println("Transmitting BLE message via LoRa");

            uint8_t buffer[256]; // Max LoRa payload
            int length = msg.serialize(buffer, sizeof(buffer));

            if (length > 0)
            {
                if (ports.lora->startTransmit(buffer, (size_t)length))
                {
                    ports.activity->markActivity();
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
    unsigned long now = ports.system->millis();
    if (now - lastBatteryUpdate >= BATTERY_UPDATE_INTERVAL)
    {
        uint8_t batteryLevel = ports.power->readBatteryLevel();
        ports.ble->updateBatteryLevel(batteryLevel);

        Serial.print("Battery: ");
        Serial.print(batteryLevel);
        Serial.println("%");

        lastBatteryUpdate = now;
    }

    // Check for inactivity timeout
    if (ports.activity->isBleConnected())
    {
        unsigned long inactiveTime = ports.activity->getInactivityDuration();
        if (inactiveTime > getPowerInactivityTimeout())
        {
            Serial.println("Inactivity timeout - disconnecting BLE");
            ports.ble->disconnect();
        }
    }

    // Small delay to prevent busy-waiting
    ports.system->delay(10);
}

// ============================================================================
// Callback Implementations
// ============================================================================

void onBleConnected()
{
    Serial.println("BLE connected");
    ports.activity->onBleConnected();
    ports.activity->markActivity();
    ports.system->ledOn();
}

void onBleDisconnected()
{
    Serial.println("BLE disconnected");
    ports.activity->onBleDisconnected();
    ports.system->ledOff();
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
            ports.activity->markActivity();
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

    // Blink LED
    ports.system->ledBlink(getLedRxBlinks());
}

void onLoRaTransmitted(bool success)
{
    if (success)
    {
        Serial.println("LoRa transmission successful");
        ports.system->ledBlink(getLedTxBlinks());
    }
    else
    {
        Serial.println("LoRa transmission failed");
    }
}

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
