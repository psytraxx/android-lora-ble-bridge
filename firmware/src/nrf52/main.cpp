#include <Arduino.h>

#include "nrf52/BLEManager.h"
#include "nrf52/LoRaManager.h"
#include "nrf52/PowerManager.h"
#include "nrf52/ApplicationController.h"
#include "nrf52/MessageBuffer.h"
#include "nrf52/FirmwareConfig.h"
#include "Protocol.h"

// Global managers
BLEManager *bleManager = nullptr;
LoRaManager *loraManager = nullptr;
PowerManager *powerManager = nullptr;
ApplicationController *appController = nullptr;
MessageBuffer *messageBuffer = nullptr;

// Message queues (simple circular buffers)
MessageQueue bleToLoraQueue;
MessageQueue loraToBleQueue;

// Timing variables
unsigned long lastBatteryUpdate = 0;
const unsigned long BATTERY_UPDATE_INTERVAL = 60000; // 1 minute

// Forward declarations
void onBLEConnected();
void onBLEDisconnected();
void onLoRaReceived(const LoRaPacket &packet);
void onLoRaTransmitted(bool success);

void setup()
{
    // Initialize Serial
    Serial.begin(115200);
    while (!Serial && millis() < 3000)
        ; // Wait up to 3s for Serial
    delay(500);

    Serial.println("\n\n=== nRF52 LoRa-BLE Bridge ===");
    Serial.print("Device: ");
    Serial.println(DEVICE_NAME);

    // Initialize LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // LED on during setup

    // Initialize application controller
    appController = new ApplicationController();

    // Initialize message buffer (persistent storage)
    messageBuffer = new MessageBuffer();
    if (!messageBuffer->begin())
    {
        Serial.println("Message buffer initialization failed!");
    }

    // Initialize power manager
    powerManager = new PowerManager();
    if (!powerManager->begin())
    {
        Serial.println("Power manager initialization failed!");
    }

    // Initialize BLE manager
    bleManager = new BLEManager(&bleToLoraQueue);
    if (!bleManager->setup(DEVICE_NAME))
    {
        Serial.println("BLE initialization failed!");
        while (1)
            ;
    }
    bleManager->setConnectionCallbacks(onBLEConnected, onBLEDisconnected);
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

    digitalWrite(LED_PIN, LOW); // LED off after setup
    Serial.println("Setup complete!");
}

void loop()
{
    // Process LoRa events (RX/TX completion)
    loraManager->process();

    // Forward messages from LoRa to BLE
    Message msg;
    if (!loraToBleQueue.isEmpty())
    {
        if (loraToBleQueue.pop(msg))
        {
            if (bleManager->isConnected() && bleManager->areNotificationsEnabled())
            {
                if (bleManager->sendMessage(msg))
                {
                    appController->incrementMessagesSent();
                    appController->markActivity();
                }
                else
                {
                    Serial.println("Failed to send message to BLE");
                }
            }
            else
            {
                // BLE not ready, buffer message for later
                Serial.println("BLE not ready, buffering message to flash");
                messageBuffer->add(msg);
            }
        }
    }

    // Send buffered messages when BLE becomes ready
    if (bleManager->isConnected() && bleManager->areNotificationsEnabled() && !messageBuffer->isEmpty())
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
                appController->incrementMessagesSent();
                appController->markActivity();
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
                    appController->markActivity();
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
        uint8_t batteryLevel = powerManager->readBatteryLevel();
        bleManager->updateBatteryLevel(batteryLevel);

        Serial.print("Battery: ");
        Serial.print(batteryLevel);
        Serial.println("%");

        lastBatteryUpdate = now;
    }

    // Check for inactivity timeout
    if (appController->isBLEConnected())
    {
        unsigned long inactiveTime = appController->getTimeSinceLastActivity();
        if (inactiveTime > PowerConstants::INACTIVITY_TIMEOUT_MS)
        {
            Serial.println("Inactivity timeout - disconnecting BLE");
            bleManager->disconnect();
        }
    }

    // Small delay to prevent busy-waiting
    delay(10);
}

//==============================================================================
// Callback Implementations
//==============================================================================

void onBLEConnected()
{
    Serial.println("Callback: BLE connected");
    appController->setBLEConnected(true);
    appController->markActivity();
    digitalWrite(LED_PIN, HIGH); // LED on when connected
}

void onBLEDisconnected()
{
    Serial.println("Callback: BLE disconnected");
    appController->setBLEConnected(false);
    digitalWrite(LED_PIN, LOW); // LED off when disconnected
}

void onLoRaReceived(const LoRaPacket &packet)
{
    Serial.println("Callback: LoRa packet received");

    // Deserialize message
    Message msg;
    if (msg.deserialize(packet.buffer, packet.len))
    {
        Serial.print("Message type: ");
        Serial.println((int)msg.type);

        // Forward to BLE
        if (loraToBleQueue.push(msg))
        {
            appController->incrementMessagesReceived();
            Serial.println("Message queued for BLE");
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
    for (int i = 0; i < LEDConstants::RX_BLINKS; i++)
    {
        digitalWrite(LED_PIN, HIGH);
        delay(LEDConstants::BLINK_DURATION_MS);
        digitalWrite(LED_PIN, LOW);
        if (i < LEDConstants::RX_BLINKS - 1)
        {
            delay(LEDConstants::BLINK_DELAY_MS);
        }
    }
}

void onLoRaTransmitted(bool success)
{
    if (success)
    {
        Serial.println("Callback: LoRa transmission successful");

        // Blink LED
        for (int i = 0; i < LEDConstants::TX_BLINKS; i++)
        {
            digitalWrite(LED_PIN, HIGH);
            delay(LEDConstants::BLINK_DURATION_MS);
            digitalWrite(LED_PIN, LOW);
            if (i < LEDConstants::TX_BLINKS - 1)
            {
                delay(LEDConstants::BLINK_DELAY_MS);
            }
        }
    }
    else
    {
        Serial.println("Callback: LoRa transmission failed");
    }
}
