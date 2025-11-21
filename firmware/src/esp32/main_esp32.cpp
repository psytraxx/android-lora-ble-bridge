//! ESP32 Main - Template-Based Application
//!
//! Instantiates the Application template with ESP32-specific managers.

#include <Arduino.h>
#include <Adafruit_SleepyDog.h>
#include "Application.h"
#include "esp32/BLEManager.h"
#include "esp32/LoRaManager.h"
#include "esp32/MessageBuffer.h"
#include "esp32/PowerManager.h"
#include "esp32/LEDManager.h"
#include "esp32/ApplicationController.h"
#include "esp32/FirmwareConfig.h"

// ============================================================================
// ESP32 Manager Wrappers
// ============================================================================

/**
 * @brief ESP32 System Manager (wraps LEDManager, Watchdog, timing)
 */
class ESP32SystemManager
{
public:
    ESP32SystemManager()
#ifdef LED_PIN
        : ledManager(LED_PIN)
#endif
    {
#ifdef LED_PIN
        ledManager.setup();
#endif
        // Initialize watchdog
        int watchdogMS = Watchdog.enable(WatchdogConstants::TIMEOUT_SECONDS * 1000);
        Serial.print("Watchdog enabled: ");
        Serial.print(watchdogMS);
        Serial.println(" ms");
    }

    void watchdogReset() { Watchdog.reset(); }

    void ledOn()
    {
#ifdef LED_PIN
        ledManager.setOn();
#endif
    }

    void ledOff()
    {
#ifdef LED_PIN
        ledManager.setOff();
#endif
    }

    void ledBlink(int count)
    {
#ifdef LED_PIN
        ledManager.blink(count);
#endif
    }

    void delay(unsigned long ms) { ::delay(ms); }
    unsigned long millis() { return ::millis(); }

    int getLedRxBlinks() const { return LEDConstants::RX_BLINKS; }
    int getLedTxBlinks() const { return LEDConstants::TX_BLINKS; }

private:
#ifdef LED_PIN
    LEDManager ledManager;
#endif
};

/**
 * @brief ESP32 Activity Manager (wraps ApplicationController)
 */
class ESP32ActivityManager
{
public:
    ESP32ActivityManager() : appController() {}

    void markActivity() { appController.notifyActivity(); }
    unsigned long getInactivityDuration() { return appController.getInactivityDuration(); }
    unsigned long getInactivityTimeout() const { return PowerConstants::INACTIVITY_TIMEOUT_MS; }
    void onBleConnected() { appController.onBleConnected(); }
    void onBleDisconnected() { appController.onBleDisconnected(); }
    bool isBleConnected() { return appController.isConnected(); }

private:
    ApplicationController appController;
};

// ESP32's BLEManager is already compatible - just need to add processIncomingMessages
// We can't inherit from BLEManager (not designed for inheritance), so we'll use
// a simple approach: don't wrap it, and the template will fail to compile
// processIncomingMessages. Instead, remove that call from the template.

/**
 * @brief ESP32 Power Manager (static wrapper)
 */
class ESP32PowerManagerWrapper
{
public:
    bool begin()
    {
        PowerManager::configurePowerManagement();
        return true;
    }

    uint8_t readBatteryLevel() { return PowerManager::readBatteryLevel(); }
};

// ============================================================================
// Application Type Definition
// ============================================================================

using ESP32Application = Application<
    ESP32BLEManagerWrapper,
    LoRaManager,
    MessageBuffer,
    ESP32PowerManagerWrapper,
    ESP32SystemManager,
    ESP32ActivityManager>;

// ============================================================================
// Global Application Instance
// ============================================================================

ESP32Application *app = nullptr;

// ============================================================================
// Arduino Entry Points
// ============================================================================

void setup()
{
    // Create managers
    ESP32BLEManagerWrapper ble;

    LoRaManager lora(
        LORA_SCK,
        LORA_MISO,
        LORA_MOSI,
        LORA_SS,
        LORA_RST,
        LORA_DIO0,
        LORA_BUSY);

    MessageBuffer storage;
    ESP32PowerManagerWrapper power;
    ESP32SystemManager system;
    ESP32ActivityManager activity;

    // Create application
    app = new ESP32Application(
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
