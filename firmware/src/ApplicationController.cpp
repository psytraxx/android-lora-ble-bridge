#include "ApplicationController.h"
#include "LEDManager.h"
#include "PowerManager.h"
#include <Adafruit_SleepyDog.h>

// External LED manager reference (if defined)
#ifdef LED_PIN
extern LEDManager ledManager;
#endif

ApplicationController::ApplicationController()
    : bleManager(nullptr),
      loraManager(nullptr),
      messageBuffer(nullptr),
      bleToLoraQueue(nullptr),
      loraToBleQueue(nullptr),
      state(AppState::DISCONNECTED_ADVERTISING),
      previousState(AppState::DISCONNECTED_ADVERTISING),
      advertiseStartMillis(0),
      lastActivityMillis(0),
      connectionEstablishedMillis(0),
      wasConnected(false)
{
}

void ApplicationController::begin(
    BLEManager *bleMgr,
    LoRaManager *loraMgr,
    MessageBuffer *msgBuffer,
    QueueHandle_t bleToLoraQ,
    QueueHandle_t loraToBleQ)
{
    bleManager = bleMgr;
    loraManager = loraMgr;
    messageBuffer = msgBuffer;
    bleToLoraQueue = bleToLoraQ;
    loraToBleQueue = loraToBleQ;

    // Initialize state
    state = AppState::DISCONNECTED_ADVERTISING;
    previousState = state;
    advertiseStartMillis = millis();
    lastActivityMillis = millis();
    wasConnected = false;

    Serial.println("ApplicationController: Initialized");
}

void ApplicationController::update()
{
    // Process state machine
    processStateMachine();

    // Process message queues
    processBleToLoraQueue();
    processLoRaToBleQueue();
}

void ApplicationController::processStateMachine()
{
    bool isCurrentlyConnected = bleManager->isConnected();

    // Detect connection state changes
    if (isCurrentlyConnected && !wasConnected)
    {
        // New connection established
        connectionEstablishedMillis = millis();
        transitionTo(AppState::CONNECTED_ACTIVE);
        Serial.println("BLE connected");
    }
    else if (!isCurrentlyConnected && wasConnected)
    {
        // Connection lost
        transitionTo(AppState::DISCONNECTED_ADVERTISING);
        Serial.println("BLE disconnected");
    }

    // Debug logging for connection state issues
    if (isCurrentlyConnected != wasConnected)
    {
        Serial.printf("Connection state change detected - isConnected=%d, wasConnected=%d, state=%d\n",
                      isCurrentlyConnected, wasConnected, (int)state);
    }

    wasConnected = isCurrentlyConnected;

    // State-specific logic
    switch (state)
    {
    case AppState::DISCONNECTED_ADVERTISING:
        handleDisconnectedAdvertising();
        break;

    case AppState::CONNECTED_ACTIVE:
        handleConnectedActive();
        break;
    }
}

void ApplicationController::handleDisconnectedAdvertising()
{
    // Start advertising if not already started
    if (advertiseStartMillis == 0)
    {
        Serial.println("Starting BLE advertising");
        bleManager->startAdvertising();
        advertiseStartMillis = millis();
    }

    // Check for advertising timeout
    unsigned long advertisingDuration = (millis()) - advertiseStartMillis;
    if (advertisingDuration >= PowerConstants::ADVERTISE_DURATION_MS)
    {
        Serial.printf("Advertising timeout (%d ms) - entering deep sleep", PowerConstants::ADVERTISE_DURATION_MS);

        // Stop advertising before sleep
        bleManager->stopAdvertising();

        // Enter deep sleep (does not return - device will reset on wake)
        PowerManager::enterDeepSleep();
    }
}

void ApplicationController::handleConnectedActive()
{
    // Forward buffered messages if Android is ready
    forwardBufferedMessages();

    // Check for inactivity
    unsigned long idleTime = (millis()) - lastActivityMillis;
    if (idleTime > PowerConstants::INACTIVITY_TIMEOUT_MS)
    {
        Serial.println("Inactivity timeout - forcing disconnect");
        bleManager->disconnect();
        // State will transition to DISCONNECTED_ADVERTISING on next update
    }
}

void ApplicationController::transitionTo(AppState newState)
{
    if (state == newState)
    {
        return;
    }

    previousState = state;
    state = newState;

    // Log state transition
    Serial.printf("State transition: %d → %d\n", (int)previousState, (int)newState);

    // State entry actions
    switch (newState)
    {
    case AppState::DISCONNECTED_ADVERTISING:
        // Reset advertising timer so it will restart fresh
        advertiseStartMillis = 0;
        break;

    case AppState::CONNECTED_ACTIVE:
        // Stop advertising when connected
        bleManager->stopAdvertising();
        lastActivityMillis = esp_timer_get_time() / 1000ULL;
        break;
    }
}

void ApplicationController::processBleToLoraQueue()
{
    Message bleMsg;
    if (xQueueReceive(bleToLoraQueue, &bleMsg, 0) == pdTRUE)
    {
        Serial.printf("BLE → LoRa, type=%d\n", (int)bleMsg.type);

        // Serialize and transmit via LoRa
        uint8_t buf[BufferConstants::MAX_PROTOCOL_MESSAGE];
        int len = bleMsg.serialize(buf, sizeof(buf));

        if (len > 0)
        {
            // Reset watchdog before long LoRa transmission
            Watchdog.reset();

            // Start non-blocking transmission via LoRaManager
            if (loraManager->startTransmit(buf, len))
            {
#ifdef LED_PIN
                ledManager.blink(LEDConstants::TX_BLINKS);
#endif
            }
        }
        else
        {
            Serial.println("Failed to serialize message for LoRa TX");
        }

        // Update activity
        notifyActivity();
    }
}

void ApplicationController::processLoRaToBleQueue()
{
    Message loraMsg;
    if (xQueueReceive(loraToBleQueue, &loraMsg, 0) == pdTRUE)
    {
        Serial.printf("LoRa → BLE, type=%d\n", (int)loraMsg.type);

        // Use state machine state instead of direct isConnected() check to avoid race conditions
        bool isConnected = (state == AppState::CONNECTED_ACTIVE);

        // Debug: log the state when message arrives
        Serial.printf("Current state=%d, isConnected=%d, bleManager->isConnected()=%d\n",
                      (int)state, isConnected, bleManager->isConnected());

        if (isConnected)
        {
            // Check if Android is ready (wait for GATT discovery)
            unsigned long timeSinceConnection = (millis()) - connectionEstablishedMillis;
            if (timeSinceConnection >= BLEConstants::CONNECTION_SETUP_DELAY_MS)
            {
                // Send directly via BLE
                if (bleManager->sendMessage(loraMsg))
                {
                    Serial.println("Message forwarded to BLE");
#ifdef LED_PIN
                    ledManager.blink(LEDConstants::RX_BLINKS);
#endif
                }
                else
                {
                    // Send failed, buffer it
                    messageBuffer->add(loraMsg);
                    Serial.println("BLE send failed, buffered message");
                }
            }
            else
            {
                // Android not ready yet, buffer message
                messageBuffer->add(loraMsg);
                Serial.println("Android not ready, buffered message");
            }
        }
        else
        {
            // BLE disconnected, buffer message
            messageBuffer->add(loraMsg);
            Serial.printf("BLE disconnected, buffered (total: %d)\n", messageBuffer->getCount());
        }

        // Update activity
        notifyActivity();
    }
}

void ApplicationController::forwardBufferedMessages()
{
    if (!bleManager->isConnected() || messageBuffer->isEmpty())
    {
        return;
    }

    // Wait for Android BLE stack to be ready
    unsigned long timeSinceConnection = (millis()) - connectionEstablishedMillis;
    if (timeSinceConnection < BLEConstants::CONNECTION_SETUP_DELAY_MS)
    {
        return; // Too soon, Android still setting up
    }

    Serial.printf("Forwarding %d buffered messages\n", messageBuffer->getCount());

    // Drain buffer
    Message bufferedMsg;
    while (messageBuffer->peek(bufferedMsg))
    {
        if (bleManager->sendMessage(bufferedMsg))
        {
            // Message sent successfully
            messageBuffer->popFront();
            Serial.println("Buffered message sent");
#ifdef LED_PIN
            ledManager.blink(LEDConstants::RX_BLINKS);
#endif
            // Spacing between messages to avoid overwhelming BLE stack
            delay(BLEConstants::MESSAGE_SPACING_MS);
        }
        else
        {
            Serial.println("Failed to send buffered message");
            break; // Stop trying, keep message in buffer
        }
    }
}

void ApplicationController::notifyActivity()
{
    lastActivityMillis = millis();
}

bool ApplicationController::hasActivity() const
{
    return (uxQueueMessagesWaiting(bleToLoraQueue) > 0) ||
           (uxQueueMessagesWaiting(loraToBleQueue) > 0);
}

int ApplicationController::getLoopDelay() const
{
    if (hasActivity())
    {
        return LoopConstants::ACTIVE_DELAY_MS;
    }
    else
    {
        return LoopConstants::IDLE_DELAY_MS;
    }
}
