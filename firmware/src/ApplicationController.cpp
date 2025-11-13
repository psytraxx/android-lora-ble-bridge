#include "ApplicationController.h"
#include <esp_timer.h>
#include "esp_log.h"

static const char *TAG = "AppState";

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
        ESP_LOGE(TAG, "Failed to create state mutex!");
    }
}

void ApplicationController::begin()
{
    xSemaphoreTake(stateMutex, portMAX_DELAY);

    // Initialize state
    state = AppState::DISCONNECTED_ADVERTISING;
    advertiseStartMillis = getCurrentTimeMillis();
    lastActivityMillis = getCurrentTimeMillis();
    connectionEstablishedMillis = 0;

    xSemaphoreGive(stateMutex);

    ESP_LOGI(TAG, "ApplicationController initialized (pure state machine)");
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
        ESP_LOGI(TAG, "State transition: DISCONNECTED_ADVERTISING → CONNECTED_ACTIVE");
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
        ESP_LOGI(TAG, "State transition: CONNECTED_ACTIVE → DISCONNECTED_ADVERTISING");
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
    return getConnectionDuration() >= BLEConstants::CONNECTION_SETUP_DELAY_MS;
}

// ============================================================================
// Private Helper
// ============================================================================

unsigned long ApplicationController::getCurrentTimeMillis() const
{
    return esp_timer_get_time() / 1000ULL;
}
