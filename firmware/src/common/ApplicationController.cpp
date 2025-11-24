#include "common/ApplicationController.h"
#include "common/Logging.h"
#include <Arduino.h>

static const char *TAG = "App";

ApplicationController::ApplicationController()
    : stateMutex(nullptr),
      state(AppState::DISCONNECTED_ADVERTISING),
      advertiseStartMillis(0),
      lastActivityMillis(0),
      connectionEstablishedMillis(0)
{
    // Create mutex for thread-safe state access
    stateMutex = xSemaphoreCreateMutex();
    if (stateMutex == nullptr)
    {
        LOG_E(TAG, "Failed to create state mutex!");
    }
}

bool ApplicationController::begin()
{
    // Check if mutex was created successfully in constructor
    if (stateMutex == nullptr)
    {
        LOG_E(TAG, "Cannot initialize - mutex creation failed");
        return false;
    }

    xSemaphoreTake(stateMutex, portMAX_DELAY);

    // Initialize state
    state = AppState::DISCONNECTED_ADVERTISING;
    advertiseStartMillis = getCurrentTimeMillis();
    lastActivityMillis = getCurrentTimeMillis();
    connectionEstablishedMillis = 0;

    xSemaphoreGive(stateMutex);

    LOG_I(TAG, "Initialized (pure state machine)");
    return true;
}

// ============================================================================
// State Queries (Thread-Safe)
// ============================================================================

AppState ApplicationController::getState() const
{
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    AppState currentState = state;
    xSemaphoreGive(stateMutex);
    return currentState;
}

bool ApplicationController::isConnected() const
{
    return getState() == AppState::CONNECTED_ACTIVE;
}

// ============================================================================
// State Transitions (Thread-Safe)
// ============================================================================

void ApplicationController::onBleConnected()
{
    xSemaphoreTake(stateMutex, portMAX_DELAY);

    if (state != AppState::CONNECTED_ACTIVE)
    {
        LOG_I(TAG, "State transition: DISCONNECTED_ADVERTISING → CONNECTED_ACTIVE");
        state = AppState::CONNECTED_ACTIVE;
        connectionEstablishedMillis = getCurrentTimeMillis();
        lastActivityMillis = getCurrentTimeMillis();
    }

    xSemaphoreGive(stateMutex);
}

void ApplicationController::onBleDisconnected()
{
    xSemaphoreTake(stateMutex, portMAX_DELAY);

    if (state != AppState::DISCONNECTED_ADVERTISING)
    {
        LOG_I(TAG, "State transition: CONNECTED_ACTIVE → DISCONNECTED_ADVERTISING");
        state = AppState::DISCONNECTED_ADVERTISING;
        advertiseStartMillis = getCurrentTimeMillis(); // Restart advertising timer
        connectionEstablishedMillis = 0;
    }

    xSemaphoreGive(stateMutex);
}

// ============================================================================
// Timestamp Management (Thread-Safe)
// ============================================================================

void ApplicationController::notifyActivity()
{
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    lastActivityMillis = getCurrentTimeMillis();
    xSemaphoreGive(stateMutex);
}

unsigned long ApplicationController::getAdvertisingDuration() const
{
    xSemaphoreTake(stateMutex, portMAX_DELAY);

    unsigned long duration = 0;
    if (state == AppState::DISCONNECTED_ADVERTISING && advertiseStartMillis > 0)
    {
        duration = getCurrentTimeMillis() - advertiseStartMillis;
    }

    xSemaphoreGive(stateMutex);
    return duration;
}

unsigned long ApplicationController::getInactivityDuration() const
{
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    unsigned long duration = getCurrentTimeMillis() - lastActivityMillis;
    xSemaphoreGive(stateMutex);
    return duration;
}

unsigned long ApplicationController::getConnectionDuration() const
{
    xSemaphoreTake(stateMutex, portMAX_DELAY);

    unsigned long duration = 0;
    if (state == AppState::CONNECTED_ACTIVE && connectionEstablishedMillis > 0)
    {
        duration = getCurrentTimeMillis() - connectionEstablishedMillis;
    }

    xSemaphoreGive(stateMutex);
    return duration;
}

bool ApplicationController::isAndroidReady() const
{
    return getConnectionDuration() >= 1000; // 1000ms delay for Android GATT setup
}

// ============================================================================
// Private Helper
// ============================================================================

unsigned long ApplicationController::getCurrentTimeMillis() const
{
    return millis();
}
