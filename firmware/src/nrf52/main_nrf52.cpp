//! nRF52 Main - Template-Based Application
//!
//! Instantiates the Application template with nRF52-specific managers.

#include <Arduino.h>
#include "Application.h"
#include "nrf52/BLEManager.h"
#include "nrf52/LoRaManager.h"
#include "nrf52/MessageBuffer.h"
#include "nrf52/PowerManager.h"
#include "nrf52/ApplicationController.h"
#include "nrf52/FirmwareConfig.h"

// ============================================================================
// nRF52 Manager Wrappers
// ============================================================================

/**
 * @brief nRF52 BLE Manager Wrapper
 *
 * The nRF52 BLEManager needs a queue pointer for incoming messages.
 * This wrapper adapts it to work with the Application template's callback model.
 */
class NRF52BLEManagerWrapper
{
public:
    NRF52BLEManagerWrapper() : bleManager(&incomingQueue), messageCallback(nullptr) {}

    bool setup(const char *deviceName) { return bleManager.setup(deviceName); }
    void startAdvertising() { bleManager.startAdvertising(); }
    bool isConnected() { return bleManager.isConnected() && bleManager.areNotificationsEnabled(); }
    bool sendMessage(const Message &msg) { return bleManager.sendMessage(msg); }
    void updateBatteryLevel(uint8_t level) { bleManager.updateBatteryLevel(level); }
    void disconnect() { bleManager.disconnect(); }

    void setConnectionCallbacks(std::function<void()> onConnect, std::function<void()> onDisconnect)
    {
        bleManager.setConnectionCallbacks(
            [onConnect]() { onConnect(); },
            [onDisconnect]() { onDisconnect(); });
    }

    void setMessageCallback(std::function<void(const Message &)> callback)
    {
        messageCallback = callback;
    }

    // Process incoming messages from the queue and invoke callback
    void processIncomingMessages()
    {
        if (messageCallback)
        {
            Message msg;
            while (incomingQueue.pop(msg))
            {
                messageCallback(msg);
            }
        }
    }

private:
    BLEManager bleManager;
    MessageQueue incomingQueue;
    std::function<void(const Message &)> messageCallback;
};

/**
 * @brief nRF52 System Manager (direct GPIO LED, no watchdog)
 */
class NRF52SystemManager
{
public:
    NRF52SystemManager()
    {
        pinMode(LED_PIN, OUTPUT);
        digitalWrite(LED_PIN, LOW);
    }

    void watchdogReset() {} // nRF52 doesn't use watchdog

    void ledOn() { digitalWrite(LED_PIN, HIGH); }
    void ledOff() { digitalWrite(LED_PIN, LOW); }

    void ledBlink(int count)
    {
        for (int i = 0; i < count; i++)
        {
            digitalWrite(LED_PIN, HIGH);
            ::delay(LEDConstants::BLINK_DURATION_MS);
            digitalWrite(LED_PIN, LOW);
            if (i < count - 1)
            {
                ::delay(LEDConstants::BLINK_DELAY_MS);
            }
        }
    }

    void delay(unsigned long ms) { ::delay(ms); }
    unsigned long millis() { return ::millis(); }

    int getLedRxBlinks() const { return LEDConstants::RX_BLINKS; }
    int getLedTxBlinks() const { return LEDConstants::TX_BLINKS; }
};

/**
 * @brief nRF52 Activity Manager (wraps ApplicationController)
 */
class NRF52ActivityManager
{
public:
    NRF52ActivityManager() : appController() {}

    void markActivity() { appController.markActivity(); }
    unsigned long getInactivityDuration() { return appController.getTimeSinceLastActivity(); }
    unsigned long getInactivityTimeout() const { return PowerConstants::INACTIVITY_TIMEOUT_MS; }
    void onBleConnected() { appController.setBLEConnected(true); }
    void onBleDisconnected() { appController.setBLEConnected(false); }
    bool isBleConnected() { return appController.isBLEConnected(); }

private:
    ApplicationController appController;
};

// ============================================================================
// Application Type Definition
// ============================================================================

using NRF52Application = Application<
    NRF52BLEManagerWrapper,
    LoRaManager,
    MessageBuffer,
    PowerManager,
    NRF52SystemManager,
    NRF52ActivityManager>;

// ============================================================================
// Global Application Instance
// ============================================================================

NRF52Application *app = nullptr;

// ============================================================================
// Arduino Entry Points
// ============================================================================

void setup()
{
    // Create managers
    NRF52BLEManagerWrapper ble;

    LoRaManager lora(
        LORA_SCK,
        LORA_MISO,
        LORA_MOSI,
        LORA_SS,
        LORA_RST,
        LORA_DIO0,
        LORA_BUSY);

    MessageBuffer storage;
    PowerManager power;
    NRF52SystemManager system;
    NRF52ActivityManager activity;

    // Create application
    app = new NRF52Application(
        std::move(ble),
        std::move(lora),
        std::move(storage),
        std::move(power),
        std::move(system),
        std::move(activity));

    // LoRa configuration
    LoRaConfig loraConfig = {
        .frequency = LORA_FREQUENCY,
        .bandwidth = LORA_BANDWIDTH,
        .spreadingFactor = LORA_SPREADING_FACTOR,
        .codingRate = LORA_CODING_RATE,
        .txPower = LORA_TX_POWER};

    // Initialize
    app->setup(DEVICE_NAME, loraConfig);
}

void loop()
{
    app->loop();
}
