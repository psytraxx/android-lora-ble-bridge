// PowerController.cpp
#include "PowerController.h"
#include <esp_sleep.h>

PowerController *PowerController::instance = nullptr;

// Note: New deep-sleep-first policy uses pairing windows and RTC-backed buffer

PowerController::PowerController()
    : bleManager(nullptr), messageBuffer(nullptr), state(STATE_DEEP_SLEEP), advertiseStartMillis(0), lastActivityMillis(0)
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

    // Start in deep sleep by default; main will decide whether to start pairing window
    advertiseStartMillis = 0;
    state = STATE_DEEP_SLEEP;
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
    (void)0;
    bool connected = (bleManager && bleManager->isConnected());

    // If connected -> ensure we're in CONNECTED state and manage paired timeout
    if (connected)
    {
        if (state != STATE_CONNECTED)
        {
            Serial.println("PowerController: Entering CONNECTED (paired) state");
            state = STATE_CONNECTED;
            bleManager->stopAdvertising();
        }

        if (lastActivityMillis == 0)
            lastActivityMillis = millis();

        // Paired idle timeout -> disconnect and go to deep sleep
        if ((millis() - lastActivityMillis) > PAIRED_TIMEOUT_MS)
        {
            Serial.println("PowerController: Paired idle timeout - disconnecting and deep sleep");
            bleManager->disconnect();
            // Immediately enter deep sleep
            enterDeepSleepNow();
        }

        return;
    }

    // Not connected
    if (state == STATE_CONNECTED)
    {
        // We were connected but now disconnected -> go to deep sleep immediately
        Serial.println("PowerController: BLE disconnected - entering deep sleep");
        enterDeepSleepNow();
        return;
    }

    // If we're in advertising window, check timeout
    if (state == STATE_ADVERTISING_WINDOW)
    {
        if (advertiseStartMillis == 0)
        {
            // Ensure advertising is active
            Serial.println("PowerController: startAdvertising (pairing window)");
            bleManager->startAdvertising();
            advertiseStartMillis = millis();
        }

        // If pairing window expired -> go to deep sleep
        if ((millis() - advertiseStartMillis) >= PAIR_WINDOW_MS)
        {
            Serial.println("PowerController: Pairing window expired - entering deep sleep");
            enterDeepSleepNow();
        }
    }
}

void PowerController::startPairingWindow()
{
    Serial.println("PowerController: startPairingWindow requested");
    state = STATE_ADVERTISING_WINDOW;
    advertiseStartMillis = 0; // will be set in update when advertising starts
    lastActivityMillis = 0;
    // Ensure BLE advertising will be started by update() path
}

void PowerController::enterDeepSleepNow()
{
    Serial.println("PowerController: EEntering deep sleep");

    // Small delay to let stacks settle
    delay(20);
    // Block until wake
    esp_deep_sleep_start();
}
