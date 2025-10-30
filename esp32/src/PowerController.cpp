// PowerController.cpp
#include "PowerController.h"
#include <esp_sleep.h>

PowerController *PowerController::instance = nullptr;

// Configuration: advertise and light-sleep durations (ms)
static const unsigned long ADVERTISE_MS = 60000UL;   // 60 seconds advertise window
static const unsigned long LIGHT_SLEEP_MS = 10000UL; // 10 seconds light sleep (change to 5000 for quick debug)

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

    // Disconnected states: advertising for 30s, then light sleep for 30s
    if (state == STATE_DISCONNECTED_ADVERTISING)
    {
        // Ensure advertising is active; set advertiseStartMillis when advertising begins
        if (advertiseStartMillis == 0)
        {
            Serial.println("PowerController: startAdvertising");
            bleManager->startAdvertising();
            advertiseStartMillis = millis();
        }

        // (Removed periodic debug logs to reduce console noise)

        if (millis() - advertiseStartMillis >= ADVERTISE_MS)
        {
            Serial.print("PowerController: Advertising period ended (timeout=");
            Serial.print(ADVERTISE_MS);
            Serial.print(" ms) - entering light sleep for ");
            Serial.print(LIGHT_SLEEP_MS);
            Serial.println(" ms");

            // Prepare for light sleep. Let LoRa GPIO wake the chip or timer wake after LIGHT_SLEEP_MS
            esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_MS * 1000ULL);
            // Note: main.cpp already enabled GPIO wake for LoRa DIO0 via gpio_wakeup_enable

            // Small delay to let BLE stop advertising gracefully
            delay(20);

            // Enter light sleep (this will block until wake)
            esp_light_sleep_start();

            // Woke up
            Serial.println("PowerController: Woke from light sleep");
            // Reset advertise timer and return to advertising state
            advertiseStartMillis = millis();
            state = STATE_DISCONNECTED_ADVERTISING;
        }
    }
}

void PowerController::enterLightSleepNow(uint64_t microseconds)
{
    Serial.print("PowerController: enterLightSleepNow for ");
    Serial.print(microseconds / 1000000ULL);
    Serial.println(" seconds");

    esp_sleep_enable_timer_wakeup(microseconds);
    // Clear advertise timer so when we wake the advertising window restarts
    advertiseStartMillis = 0;
    delay(10);
    esp_light_sleep_start();
    Serial.println("PowerController: woke from immediate light sleep");
}
