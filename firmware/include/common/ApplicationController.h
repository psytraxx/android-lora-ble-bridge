#ifndef APPLICATION_CONTROLLER_H
#define APPLICATION_CONTROLLER_H

// Platform-specific FreeRTOS includes
#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#elif defined(ARDUINO_ARCH_NRF52)
#include <FreeRTOS.h>
#include <semphr.h>
#else
#error "Unsupported platform"
#endif

/**
 * @file ApplicationController.h
 * @brief Thread-safe application state machine (pure state holder)
 *
 * REFACTORED ARCHITECTURE:
 * This class is now a pure state machine that ONLY manages state transitions
 * and timestamps. All message processing logic has been moved to dedicated
 * FreeRTOS tasks (BLE task, LoRa task, Power task).
 *
 * Responsibilities:
 *  - Store current application state (DISCONNECTED_ADVERTISING, CONNECTED_ACTIVE)
 *  - Track timestamps (connection, activity, advertising start)
 *  - Provide thread-safe state access via mutex
 *  - Simple state queries (no business logic)
 *
 * Design Goals:
 *  - Single source of truth for application state
 *  - Thread-safe for multi-task access
 *  - No dependencies on BLE/LoRa/MessageBuffer
 *  - Simple, testable state machine
 *
 * Note: Deep sleep causes device reset, so there is no SLEEPING state.
 */

/// Application state machine states
enum class AppState : uint8_t
{
    /// BLE advertising, no connection, waiting for client or timeout
    DISCONNECTED_ADVERTISING,

    /// BLE connected, actively processing messages
    CONNECTED_ACTIVE
};

/**
 * @brief Thread-safe application state machine (pure state holder)
 *
 * This is now a PURE state machine with no business logic.
 * State transitions are triggered by dedicated FreeRTOS tasks:
 *  - BLE Task: Handles BLE connection events and message processing
 *  - LoRa Task: Handles LoRa TX/RX and message processing
 *  - Power Task: Monitors timeouts and triggers sleep/disconnect
 */
class ApplicationController
{
public:
    /**
     * @brief Construct ApplicationController with mutex
     */
    ApplicationController();

    /**
     * @brief Initialize state machine
     */
    void begin();

    // ========================================================================
    // State Queries (Thread-Safe)
    // ========================================================================

    /**
     * @brief Get current application state (thread-safe)
     * @return Current state enum value
     */
    AppState getState() const;

    /**
     * @brief Check if BLE is connected (based on state)
     * @return true if in CONNECTED_ACTIVE state
     */
    bool isConnected() const;

    // ========================================================================
    // State Transitions (Thread-Safe)
    // ========================================================================

    /**
     * @brief Transition to CONNECTED_ACTIVE state
     * Called by BLE task when connection established
     */
    void onBleConnected();

    /**
     * @brief Transition to DISCONNECTED_ADVERTISING state
     * Called by BLE task when connection lost
     */
    void onBleDisconnected();

    // ========================================================================
    // Timestamp Management (Thread-Safe)
    // ========================================================================

    /**
     * @brief Update last activity timestamp (resets inactivity timer)
     * Called when BLE write or LoRa packet received
     */
    void notifyActivity();

    /**
     * @brief Get time since advertising started (milliseconds)
     * @return Milliseconds since advertising started, 0 if not advertising
     */
    unsigned long getAdvertisingDuration() const;

    /**
     * @brief Get time since last activity (milliseconds)
     * @return Milliseconds since last BLE/LoRa activity
     */
    unsigned long getInactivityDuration() const;

    /**
     * @brief Get time since connection established (milliseconds)
     * @return Milliseconds since BLE connection, 0 if not connected
     */
    unsigned long getConnectionDuration() const;

    /**
     * @brief Check if Android BLE stack is ready (500ms after connection)
     * @return true if enough time has passed for GATT setup
     */
    bool isAndroidReady() const;

private:
    // State machine (protected by mutex)
    mutable SemaphoreHandle_t stateMutex;
    AppState state;

    // Timers (milliseconds since boot) - protected by mutex
    unsigned long advertiseStartMillis;
    unsigned long lastActivityMillis;
    unsigned long connectionEstablishedMillis;

    /**
     * @brief Get current time in milliseconds (thread-safe)
     */
    unsigned long getCurrentTimeMillis() const;
};

#endif // APPLICATION_CONTROLLER_H
