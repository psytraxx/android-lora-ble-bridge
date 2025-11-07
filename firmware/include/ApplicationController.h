#ifndef APPLICATION_CONTROLLER_H
#define APPLICATION_CONTROLLER_H

#include "BLEManager.h"
#include "LoRaManager.h"
#include "MessageBuffer.h"
#include "FirmwareConfig.h"
#include <freertos/queue.h>

/**
 * @file ApplicationController.h
 * @brief Central application state machine coordinating BLE, LoRa, and power management
 *
 * This class encapsulates all application-level logic including:
 *  - Connection lifecycle management (advertising, connecting, disconnecting)
 *  - Message buffering and forwarding between BLE and LoRa
 *  - Activity tracking and inactivity timeout enforcement
 *  - Advertising duration policies
 *  - Android BLE stack setup delays
 *  - Deep sleep triggering (device resets on wake)
 *
 * Design Goals:
 *  - Single source of truth for application state
 *  - Clear state machine with explicit transitions
 *  - Decoupled from hardware details (power, GPIO)
 *  - Testable business logic
 *
 * Note: Deep sleep causes device reset, so there is no SLEEPING state.
 * When sleep conditions are met, the device enters deep sleep directly
 * and execution restarts from setup() on wake.
 */

/// Application state machine states
enum class AppState : uint8_t
{
    /// BLE advertising, no connection, waiting for client or timeout
    DISCONNECTED_ADVERTISING,

    /// BLE connected, actively processing messages (recent activity)
    CONNECTED_ACTIVE
};

/// Events that trigger state transitions
enum class AppEvent : uint8_t
{
    BLE_CONNECTED,       /// Android connected to BLE
    BLE_DISCONNECTED,    /// Android disconnected from BLE
    ACTIVITY_DETECTED,   /// BLE write or LoRa packet received
    TIMEOUT_ADVERTISING, /// Advertising duration expired (30s) - triggers deep sleep
    TIMEOUT_INACTIVITY   /// Inactivity timeout expired (60s)
};

/**
 * @brief Application state machine and message coordinator
 *
 * Responsibilities:
 *  - Manage application state transitions
 *  - Coordinate message buffering when BLE disconnected
 *  - Enforce advertising duration policy (30s before deep sleep)
 *  - Enforce inactivity timeout policy (60s before disconnect)
 *  - Handle Android BLE stack setup delays (1000ms)
 *  - Control loop delay (10ms active / 500ms idle)
 *  - Trigger deep sleep when appropriate (does not return)
 *
 * This class owns the application logic but delegates:
 *  - Hardware power control to PowerManager
 *  - BLE operations to BLEManager
 *  - LoRa operations to LoRaManager
 */
class ApplicationController
{
public:
    /**
     * @brief Construct ApplicationController
     */
    ApplicationController();

    /**
     * @brief Initialize with component references and queues
     * @param bleMgr BLE manager instance
     * @param loraMgr LoRa manager instance
     * @param msgBuffer Message buffer for offline storage
     * @param bleToLoraQ FreeRTOS queue for BLE → LoRa messages
     * @param loraToBleQ FreeRTOS queue for LoRa → BLE messages
     */
    void begin(
        BLEManager *bleMgr,
        LoRaManager *loraMgr,
        MessageBuffer *msgBuffer,
        QueueHandle_t bleToLoraQ,
        QueueHandle_t loraToBleQ);

    /**
     * @brief Update state machine (call from main loop)
     *
     * Processes state machine logic:
     *  - Checks for state transitions
     *  - Processes message queues
     *  - Handles buffering/forwarding
     *  - Enforces timeout policies
     */
    void update();

    /**
     * @brief Get recommended loop delay based on current state
     * @return Delay in milliseconds (10ms active, 500ms idle)
     */
    int getLoopDelay() const;

    /**
     * @brief Get current application state
     * @return Current state enum value
     */
    AppState getState() const { return state; }

    /**
     * @brief Notify controller of BLE/LoRa activity
     *
     * Resets inactivity timer and updates state if needed.
     * Called when:
     *  - BLE characteristic written
     *  - LoRa packet received
     */
    void notifyActivity();

    /**
     * @brief Check if system has pending work (for adaptive delay)
     * @return true if queues have messages or activity ongoing
     */
    bool hasActivity() const;

private:
    // Component references
    BLEManager *bleManager;
    LoRaManager *loraManager;
    MessageBuffer *messageBuffer;
    QueueHandle_t bleToLoraQueue;
    QueueHandle_t loraToBleQueue;

    // State machine
    AppState state;
    AppState previousState; // For detecting transitions

    // Timers (milliseconds since boot)
    unsigned long advertiseStartMillis;
    unsigned long lastActivityMillis;
    unsigned long connectionEstablishedMillis;

    // Connection tracking
    bool wasConnected;

    /**
     * @brief Process state machine transitions and actions
     */
    void processStateMachine();

    /**
     * @brief Handle DISCONNECTED_ADVERTISING state
     */
    void handleDisconnectedAdvertising();

    /**
     * @brief Handle CONNECTED_ACTIVE state
     */
    void handleConnectedActive();

    /**
     * @brief Process BLE → LoRa message queue
     */
    void processBleToLoraQueue();

    /**
     * @brief Process LoRa → BLE message queue and buffering
     */
    void processLoRaToBleQueue();

    /**
     * @brief Forward buffered messages to BLE when connected
     */
    void forwardBufferedMessages();

    /**
     * @brief Transition to new state
     * @param newState Target state
     */
    void transitionTo(AppState newState);
};

#endif // APPLICATION_CONTROLLER_H
