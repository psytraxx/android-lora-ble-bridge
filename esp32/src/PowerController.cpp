// PowerController.cpp
#include "PowerController.h"
#include <esp_sleep.h>

PowerController *PowerController::instance = nullptr;

// Configuration: advertise duration (ms)
static const unsigned long ADVERTISE_MS = 30000UL; // 30 seconds advertise window

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

        // 60s inactivity timeout -> force disconnect
        if ((millis() - lastActivityMillis) > 60000UL)
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

            // Prepare for light sleep. Will wake on boot button press or LoRa GPIO interrupt
            // Re-enable GPIO wakeup before each sleep (required on some ESP32 variants)
            // Note: gpio_wakeup_enable persists, but esp_sleep_enable_gpio_wakeup may need refresh
            esp_sleep_enable_gpio_wakeup();

            // Small delay to let BLE stop advertising gracefully
            delay(20);

            // Enter light sleep (this will block until wake)
            esp_light_sleep_start();

            // Woke up
            esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
            Serial.print("PowerController: Woke from light sleep - reason: ");
            switch (wakeup_reason)
            {
            case ESP_SLEEP_WAKEUP_GPIO:
                Serial.println("GPIO wakeup");
                break;
            default:
                Serial.print("Unknown (");
                Serial.print(wakeup_reason);
                Serial.println(")");
                break;
            }

            // Restart advertising after wake (set to 0 to trigger advertising start)
            Serial.println("PowerController: Restarting advertising after wake");
            advertiseStartMillis = 0;  // Set to 0 to trigger advertising restart
            state = STATE_DISCONNECTED_ADVERTISING;
        }
    }
}

void PowerController::enterLightSleepNow()
{
    Serial.println("PowerController: entering light sleep immediately (wakes on button press or LoRa activity)");

    // Sleep will wake on boot button press or LoRa GPIO interrupt
    // No timer - indefinite sleep until hardware event
    // Re-enable GPIO wakeup before sleep
    esp_sleep_enable_gpio_wakeup();

    // Clear advertise timer so when we wake the advertising window restarts
    advertiseStartMillis = 0;
    delay(10);
    esp_light_sleep_start();

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    Serial.print("PowerController: woke from immediate light sleep - reason: ");
    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_GPIO:
        Serial.println("GPIO (button or LoRa)");
        break;
    default:
        Serial.print("Unknown (");
        Serial.print(wakeup_reason);
        Serial.println(")");
        break;
    }

    Serial.println("PowerController: Will restart advertising on next update() call");
}
