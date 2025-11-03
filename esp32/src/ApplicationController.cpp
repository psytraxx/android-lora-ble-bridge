#include "ApplicationController.h"
#include "LEDManager.h"
#include "PowerManager.h"
#include <esp_task_wdt.h>
#include <esp_timer.h>

// External LED manager reference (if defined)
#ifdef LED_PIN
extern LEDManager ledManager;
#endif

static const char *TAG = "APP";

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
    advertiseStartMillis = esp_timer_get_time() / 1000ULL;
    lastActivityMillis = esp_timer_get_time() / 1000ULL;
    wasConnected = false;

    ESP_LOGI(TAG, "ApplicationController: Initialized");
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
        connectionEstablishedMillis = esp_timer_get_time() / 1000ULL;
        transitionTo(AppState::CONNECTED_ACTIVE);
        ESP_LOGI(TAG, "BLE connected");
    }
    else if (!isCurrentlyConnected && wasConnected)
    {
        // Connection lost
        transitionTo(AppState::DISCONNECTED_ADVERTISING);
        ESP_LOGI(TAG, "BLE disconnected");
    }

    // Debug logging for connection state issues
    if (isCurrentlyConnected != wasConnected)
    {
        ESP_LOGI(TAG, "Connection state change detected - isConnected=%d, wasConnected=%d, state=%d",
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
        ESP_LOGI(TAG, "Starting BLE advertising");
        bleManager->startAdvertising();
        advertiseStartMillis = esp_timer_get_time() / 1000ULL;
    }

    // Check for advertising timeout
    unsigned long advertisingDuration = (esp_timer_get_time() / 1000ULL) - advertiseStartMillis;
    if (advertisingDuration >= PowerConstants::ADVERTISE_DURATION_MS)
    {
        ESP_LOGI(TAG, "Advertising timeout (%d ms) - transitioning to sleep", PowerConstants::ADVERTISE_DURATION_MS);
        transitionTo(AppState::SLEEPING);
    }
}

void ApplicationController::handleConnectedActive()
{
    // Forward buffered messages if Android is ready
    forwardBufferedMessages();

    // Check for inactivity
    unsigned long idleTime = (esp_timer_get_time() / 1000ULL) - lastActivityMillis;
    if (idleTime > PowerConstants::INACTIVITY_TIMEOUT_MS)
    {
        ESP_LOGI(TAG, "Inactivity timeout - forcing disconnect");
        bleManager->disconnect();
        // State will transition to DISCONNECTED_ADVERTISING on next update
    }
}

void ApplicationController::handleSleeping()
{
    ESP_LOGI(TAG, "Entering light sleep");

    // Stop advertising before sleep
    bleManager->stopAdvertising();

    // Refresh wakeup sources before sleep (required on some ESP32 variants)
    PowerManager::refreshWakeupSources();

    // Enter light sleep (blocking call until wakeup)
    PowerManager::enterLightSleep();

    // Woke up - transition back to advertising
    ESP_LOGI(TAG, "Woke from sleep, restarting advertising");
    transitionTo(AppState::DISCONNECTED_ADVERTISING);
}

void ApplicationController::processBleToLoraQueue()
{
    Message bleMsg;
    if (xQueueReceive(bleToLoraQueue, &bleMsg, 0) == pdTRUE)
    {
        ESP_LOGI(TAG, "BLE → LoRa, type=%d", (int)bleMsg.type);

        // Serialize and transmit via LoRa
        uint8_t buf[BufferConstants::MAX_PROTOCOL_MESSAGE];
        int len = bleMsg.serialize(buf, sizeof(buf));

        if (len > 0)
        {
            // Reset watchdog before long LoRa transmission
            esp_task_wdt_reset();

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
            ESP_LOGI(TAG, "Failed to serialize message for LoRa TX");
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
        ESP_LOGI(TAG, "LoRa → BLE, type=%d", (int)loraMsg.type);

        // Use state machine state instead of direct isConnected() check to avoid race conditions
        bool isConnected = (state == AppState::CONNECTED_ACTIVE);

        // Debug: log the state when message arrives
        ESP_LOGI(TAG, "Current state=%d, isConnected=%d, bleManager->isConnected()=%d",
                 (int)state, isConnected, bleManager->isConnected());

        if (isConnected)
        {
            // Check if Android is ready (wait for GATT discovery)
            unsigned long timeSinceConnection = (esp_timer_get_time() / 1000ULL) - connectionEstablishedMillis;
            if (timeSinceConnection >= BLEConstants::CONNECTION_SETUP_DELAY_MS)
            {
                // Send directly via BLE
                if (bleManager->sendMessage(loraMsg))
                {
                    ESP_LOGI(TAG, "Message forwarded to BLE");
#ifdef LED_PIN
                    ledManager.blink(LEDConstants::RX_BLINKS);
#endif
                }
                else
                {
                    // Send failed, buffer it
                    messageBuffer->add(loraMsg);
                    ESP_LOGI(TAG, "BLE send failed, buffered message");
                }
            }
            else
            {
                // Android not ready yet, buffer message
                messageBuffer->add(loraMsg);
                ESP_LOGI(TAG, "Android not ready, buffered message");
            }
        }
        else
        {
            // BLE disconnected, buffer message
            messageBuffer->add(loraMsg);
            ESP_LOGI(TAG, "BLE disconnected, buffered (total: %d)", messageBuffer->getCount());
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
    unsigned long timeSinceConnection = (esp_timer_get_time() / 1000ULL) - connectionEstablishedMillis;
    if (timeSinceConnection < BLEConstants::CONNECTION_SETUP_DELAY_MS)
    {
        return; // Too soon, Android still setting up
    }

    ESP_LOGI(TAG, "Forwarding %d buffered messages", messageBuffer->getCount());

    // Drain buffer
    Message bufferedMsg;
    while (messageBuffer->peek(bufferedMsg))
    {
        if (bleManager->sendMessage(bufferedMsg))
        {
            // Message sent successfully
            messageBuffer->popFront();
            ESP_LOGI(TAG, "Buffered message sent");
#ifdef LED_PIN
            ledManager.blink(LEDConstants::RX_BLINKS);
#endif
            // Spacing between messages to avoid overwhelming BLE stack
            vTaskDelay(pdMS_TO_TICKS(BLEConstants::MESSAGE_SPACING_MS));
        }
        else
        {
            ESP_LOGI(TAG, "Failed to send buffered message");
            break; // Stop trying, keep message in buffer
        }
    }
}

void ApplicationController::notifyActivity()
{
    lastActivityMillis = esp_timer_get_time() / 1000ULL;
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
    ESP_LOGI(TAG, "State transition: %d → %d", (int)previousState, (int)newState);

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

    case AppState::SLEEPING:
        // Sleep will be coordinated by main loop
        bleManager->stopAdvertising();
        break;
    }
}
