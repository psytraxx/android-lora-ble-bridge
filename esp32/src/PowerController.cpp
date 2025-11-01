// PowerController.cpp
#include "PowerController.h"
#include "esp_pm.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <Arduino.h>

PowerController *PowerController::instance = nullptr;

// Configuration: advertise duration before entering light sleep
// 30 seconds provides good discoverability window while conserving power
static const unsigned long ADVERTISE_MS = 30000UL; // 30s advertise window before sleep

PowerController::PowerController()
    : bleManager(nullptr), messageBuffer(nullptr), state(STATE_DISCONNECTED_ADVERTISING), advertiseStartMillis(0), lastActivityMillis(0)
{
    instance = this;
}

void PowerController::begin(BLEManager *bleMgr, MessageBuffer *buf)
{
    bleManager = bleMgr;
    messageBuffer = buf;

    // Wire static activity callback into BLEManager
    if (bleManager)
    {
        bleManager->setActivityCallback(&PowerController::activityCallbackStatic);
    }

    // Start in advertising state
    // Leave advertiseStartMillis zero so update() sets it when advertising actually starts
    advertiseStartMillis = 0;
    state = STATE_DISCONNECTED_ADVERTISING;
}

void PowerController::activityCallbackStatic()
{
    if (instance)
        instance->resetInactivityTimer();
}

void PowerController::resetInactivityTimer()
{
    lastActivityMillis = millis();
}

void PowerController::update()
{
    (void)0; // no-op to avoid unused warnings
    // Check connection state and handle transitions
    bool connected = (bleManager && bleManager->isConnected());

    if (connected)
    {
        if (state != STATE_CONNECTED)
        {
            Serial.println("PowerController: Entering CONNECTED state (always active)");
            state = STATE_CONNECTED;
            // When connected, ensure advertising is stopped
            bleManager->stopAdvertising();
        }

        // If we haven't received activity recently, reset timer
        if (lastActivityMillis == 0)
            lastActivityMillis = millis();

        // Inactivity timeout -> force disconnect to save power
        // 60 seconds allows for casual message reading without premature disconnection
        // Note: Android app expects 30s timeout (see BleConstants.AUTO_DISCONNECT_DELAY_MS)
        const unsigned long INACTIVITY_TIMEOUT_MS = 60000UL; // 60s before auto-disconnect
        if ((millis() - lastActivityMillis) > INACTIVITY_TIMEOUT_MS)
        {
            Serial.println("PowerController: Inactivity timeout - disconnecting BLE client");
            bleManager->disconnect();
            state = STATE_DISCONNECTED_ADVERTISING;
            advertiseStartMillis = millis();
        }

        return;
    }

    // If we reach here, BLE is not connected. If we were previously in CONNECTED state
    // we need to transition back to the disconnected advertising state so the
    // advertise timer is (re-)initialized and sleep cycles resume.
    if (!connected && state == STATE_CONNECTED)
    {
        Serial.println("PowerController: BLE disconnected - switching to DISCONNECTED_ADVERTISING");
        state = STATE_DISCONNECTED_ADVERTISING;
        // Clear advertiseStartMillis so update() will re-start advertising and set the timer
        advertiseStartMillis = 0;
    }

    // Disconnected states: advertising for 30s, then light sleep until button press or LoRa activity
    if (state == STATE_DISCONNECTED_ADVERTISING)
    {
        // Ensure advertising is active; set advertiseStartMillis when advertising begins
        if (advertiseStartMillis == 0)
        {
            Serial.println("PowerController: startAdvertising");
            bleManager->startAdvertising();
            advertiseStartMillis = millis();
        }

        if (millis() - advertiseStartMillis >= ADVERTISE_MS)
        {
            Serial.print("PowerController: Advertising period ended (timeout=");
            Serial.print(ADVERTISE_MS);
            Serial.println(" ms) - entering light sleep until button press or LoRa activity");

            // Stop advertising and disable BLE before sleep (critical for low power)
            bleManager->stopAdvertising();

            // Prepare for light sleep. Will wake on boot button press or LoRa GPIO interrupt
            // Re-enable GPIO wakeup before each sleep (required on some ESP32 variants)
            // Note: gpio_wakeup_enable persists, but esp_sleep_enable_gpio_wakeup may need refresh
            esp_sleep_enable_gpio_wakeup();

            Serial.flush();

            // Enter light sleep (this will block until wake)
            esp_light_sleep_start();

            // Log wakeup reason
            esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
            Serial.print("Power: Woke from light sleep - reason: ");
            switch (wakeup_reason)
            {
            case ESP_SLEEP_WAKEUP_GPIO:
                Serial.println("GPIO");
                break;
            case ESP_SLEEP_WAKEUP_EXT0:
                Serial.println("EXT0 (wake button)");
                break;
            case ESP_SLEEP_WAKEUP_TIMER:
                Serial.println("Timer");
                break;
            default:
                Serial.print("Unknown (");
                Serial.print(wakeup_reason);
                Serial.println(")");
                break;
            }

            // Restart advertising after wake (set to 0 to trigger advertising start)
            Serial.println("PowerController: Restarting advertising after wake");
            advertiseStartMillis = 0; // Set to 0 to trigger advertising restart
            state = STATE_DISCONNECTED_ADVERTISING;
        }
    }
}

void PowerController::configurePowerManagement()
{
    Serial.println("PowerController: Configuring power management");

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    static esp_pm_config_t esp32_config; // filled with zeros because bss
#else
    static esp_pm_config_esp32_t esp32_config; // filled with zeros because bss
#endif

    esp32_config.max_freq_mhz = CPU_FREQ_MHZ;
    esp32_config.min_freq_mhz = 20;
    esp32_config.light_sleep_enable = false;
    int rv = esp_pm_configure(&esp32_config);
    if (rv != ESP_OK)
    {
        Serial.print("PowerController: Failed to configure power management (err=");
        Serial.print(rv);
        Serial.println(")");
        return;
    }
    Serial.println("Power management configured (light sleep enabled)");
}

void PowerController::configureWakeupSources(int wakeButton, int loraDio0)
{
    // Configure boot button wake (ext0 for LOW trigger on RTC GPIO)
    gpio_wakeup_enable((gpio_num_t)wakeButton, GPIO_INTR_LOW_LEVEL);
    // esp_sleep_enable_ext0_wakeup((gpio_num_t)wakeButton, 0); // 0 = LOW (button pressed)

    // Configure LoRa DIO0 wake (gpio_wakeup for HIGH trigger)
    // Add pulldown to ensure clean LOW state when idle (matches Meshtastic approach)
    gpio_pulldown_en((gpio_num_t)loraDio0);
    gpio_wakeup_enable((gpio_num_t)loraDio0, GPIO_INTR_HIGH_LEVEL);

    // Enable GPIO wakeup - must be called after gpio_wakeup_enable
    esp_sleep_enable_gpio_wakeup();

    Serial.print("GPIO wake-up configured: Boot button (GPIO");
    Serial.print(wakeButton);
    Serial.print(" on LOW), LoRa DIO0 (GPIO");
    Serial.print(loraDio0);
    Serial.println(" on HIGH)");
}