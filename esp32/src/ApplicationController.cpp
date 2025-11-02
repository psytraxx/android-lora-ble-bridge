#include "ApplicationController.h"
#include "LEDManager.h"
#include <esp_task_wdt.h>

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
        Serial.println("AppController: BLE connected");
    }
    else if (!isCurrentlyConnected && wasConnected)
    {
        // Connection lost
        transitionTo(AppState::DISCONNECTED_ADVERTISING);
        advertiseStartMillis = 0; // Will restart advertising
        Serial.println("AppController: BLE disconnected");
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

    case AppState::CONNECTED_IDLE:
        handleConnectedIdle();
        break;

    case AppState::SLEEPING:
        handleSleeping();
        break;
    }
}

void ApplicationController::handleDisconnectedAdvertising()
{
    // Start advertising if not already started
    if (advertiseStartMillis == 0)
    {
        Serial.println("AppController: Starting BLE advertising");
        bleManager->startAdvertising();
        advertiseStartMillis = millis();
    }

    // Check for advertising timeout
    unsigned long advertisingDuration = millis() - advertiseStartMillis;
    if (advertisingDuration >= PowerConstants::ADVERTISE_DURATION_MS)
    {
        Serial.print("AppController: Advertising timeout (");
        Serial.print(PowerConstants::ADVERTISE_DURATION_MS);
        Serial.println(" ms) - transitioning to sleep");
        transitionTo(AppState::SLEEPING);
    }
}

void ApplicationController::handleConnectedActive()
{
    // Forward buffered messages if Android is ready
    forwardBufferedMessages();

    // Check for inactivity
    unsigned long idleTime = millis() - lastActivityMillis;
    if (idleTime > PowerConstants::INACTIVITY_TIMEOUT_MS)
    {
        Serial.println("AppController: Inactivity timeout - forcing disconnect");
        bleManager->disconnect();
        // State will transition to DISCONNECTED_ADVERTISING on next update
    }
}

void ApplicationController::handleConnectedIdle()
{
    // Currently same as CONNECTED_ACTIVE
    // Could implement different behavior if needed (e.g., reduced processing)
    handleConnectedActive();
}

void ApplicationController::handleSleeping()
{
    // This state is entered but sleep is managed by PowerManager
    // The actual sleep blocking call would be in main loop coordination
    Serial.println("AppController: SLEEPING state (sleep managed externally)");
}

void ApplicationController::processBleToLoraQueue()
{
    Message bleMsg;
    if (xQueueReceive(bleToLoraQueue, &bleMsg, 0) == pdTRUE)
    {
        Serial.print("AppController: BLE → LoRa, type=");
        Serial.println((int)bleMsg.type);

        // Serialize and transmit via LoRa
        uint8_t buf[BufferConstants::MAX_PROTOCOL_MESSAGE];
        int len = bleMsg.serialize(buf, sizeof(buf));

        if (len > 0)
        {
            // Reset watchdog before long LoRa transmission
            esp_task_wdt_reset();

            // Transmit via LoRaManager
            if (loraManager->transmit(buf, len))
            {
#ifdef LED_PIN
                ledManager.blink(LEDConstants::TX_BLINKS);
#endif
            }
        }
        else
        {
            Serial.println("AppController: Failed to serialize message for LoRa TX");
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
        Serial.print("AppController: LoRa → BLE, type=");
        Serial.println((int)loraMsg.type);

        if (bleManager->isConnected())
        {
            // Check if Android is ready (wait for GATT discovery)
            unsigned long timeSinceConnection = millis() - connectionEstablishedMillis;
            if (timeSinceConnection >= BLEConstants::CONNECTION_SETUP_DELAY_MS)
            {
                // Send directly via BLE
                if (bleManager->sendMessage(loraMsg))
                {
                    Serial.println("AppController: Message forwarded to BLE");
#ifdef LED_PIN
                    ledManager.blink(LEDConstants::RX_BLINKS);
#endif
                }
                else
                {
                    // Send failed, buffer it
                    messageBuffer->add(loraMsg);
                    Serial.println("AppController: BLE send failed, buffered message");
                }
            }
            else
            {
                // Android not ready yet, buffer message
                messageBuffer->add(loraMsg);
                Serial.println("AppController: Android not ready, buffered message");
            }
        }
        else
        {
            // BLE disconnected, buffer message
            messageBuffer->add(loraMsg);
            Serial.print("AppController: BLE disconnected, buffered (total: ");
            Serial.print(messageBuffer->getCount());
            Serial.println(")");
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
    unsigned long timeSinceConnection = millis() - connectionEstablishedMillis;
    if (timeSinceConnection < BLEConstants::CONNECTION_SETUP_DELAY_MS)
    {
        return; // Too soon, Android still setting up
    }

    Serial.print("AppController: Forwarding ");
    Serial.print(messageBuffer->getCount());
    Serial.println(" buffered messages");

    // Drain buffer
    Message bufferedMsg;
    while (messageBuffer->peek(bufferedMsg))
    {
        if (bleManager->sendMessage(bufferedMsg))
        {
            // Message sent successfully
            messageBuffer->popFront();
            Serial.println("AppController: Buffered message sent");
#ifdef LED_PIN
            ledManager.blink(LEDConstants::RX_BLINKS);
#endif
            // Spacing between messages to avoid overwhelming BLE stack
            delay(BLEConstants::MESSAGE_SPACING_MS);
        }
        else
        {
            Serial.println("AppController: Failed to send buffered message");
            break; // Stop trying, keep message in buffer
        }
    }
}

void ApplicationController::notifyActivity()
{
    lastActivityMillis = millis();

    // Update state based on activity
    if (state == AppState::CONNECTED_IDLE)
    {
        transitionTo(AppState::CONNECTED_ACTIVE);
    }
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

void ApplicationController::transitionTo(AppState newState)
{
    if (state == newState)
    {
        return;
    }

    previousState = state;
    state = newState;

    // Log state transition
    Serial.print("AppController: State transition: ");
    Serial.print((int)previousState);
    Serial.print(" → ");
    Serial.println((int)newState);

    // State entry actions
    switch (newState)
    {
    case AppState::DISCONNECTED_ADVERTISING:
        // Stop advertising will be handled in handleDisconnectedAdvertising()
        break;

    case AppState::CONNECTED_ACTIVE:
        // Stop advertising when connected
        bleManager->stopAdvertising();
        lastActivityMillis = millis();
        break;

    case AppState::CONNECTED_IDLE:
        // No specific action
        break;

    case AppState::SLEEPING:
        // Sleep will be coordinated by main loop
        bleManager->stopAdvertising();
        break;
    }
}

bool ApplicationController::connectionStateChanged()
{
    bool currentlyConnected = bleManager->isConnected();
    bool changed = (currentlyConnected != wasConnected);
    return changed;
}
