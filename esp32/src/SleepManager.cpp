#include "SleepManager.h"

// Magic number to identify valid RTC data
#define RTC_MAGIC 0xDEADBEEF

// RTC memory data - persists across light sleep
RTC_DATA_ATTR SleepData rtcData;

SleepManager::SleepManager(int loraInterruptPin)
    : loraIntPin(loraInterruptPin), lastActivityMillis(0)
{
}

void SleepManager::setup()
{
    Serial.println("Initializing Sleep Manager...");

    // Check if we're waking from light sleep
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED)
    {
        // First boot - initialize RTC data
        Serial.println("First boot - initializing RTC memory");
        initializeRTCData();
    }
    else
    {
        // Waking from light sleep
        Serial.println(getWakeupReason());

        // Verify RTC data is valid
        if (!isRTCDataValid())
        {
            Serial.println("WARNING: RTC data corrupted, reinitializing");
            initializeRTCData();
        }
        else
        {
            rtcData.wakeupCount++;
            Serial.print("Wake-up count: ");
            Serial.println(rtcData.wakeupCount);
            Serial.print("Stored messages: ");
            Serial.println(rtcData.messageCount);
        }
    }

    // Configure LoRa interrupt pin for wake-up (active HIGH when packet received)
    pinMode(loraIntPin, INPUT);

    // Configure EXT1 for LoRa interrupt wake-up
    // EXT1 wakes when LoRa DIO0 goes HIGH (packet received)
    esp_sleep_enable_ext1_wakeup(1ULL << loraIntPin, ESP_EXT1_WAKEUP_ANY_HIGH);

    Serial.println("Sleep wake-up sources configured:");
    Serial.print("  - LoRa interrupt on GPIO ");
    Serial.println(loraIntPin);

    // Initialize activity timer
    updateActivity();
}

void SleepManager::updateActivity()
{
    lastActivityMillis = millis();
}

bool SleepManager::shouldEnterLightSleep()
{
    unsigned long currentMillis = millis();
    unsigned long inactiveTime = currentMillis - lastActivityMillis;

    return inactiveTime >= LIGHT_SLEEP_TIMEOUT_MS;
}

bool SleepManager::storeMessage(const Message &msg)
{
    if (rtcData.messageCount >= MAX_STORED_MESSAGES)
    {
        Serial.println("ERROR: RTC message buffer full!");
        return false;
    }

    rtcData.messages[rtcData.messageCount] = msg;
    rtcData.messageCount++;

    Serial.print("Message stored in RTC memory (");
    Serial.print(rtcData.messageCount);
    Serial.print("/");
    Serial.print(MAX_STORED_MESSAGES);
    Serial.println(")");

    return true;
}

uint8_t SleepManager::getStoredMessageCount()
{
    return rtcData.messageCount;
}

bool SleepManager::retrieveMessage(Message &msg)
{
    if (rtcData.messageCount == 0)
    {
        return false;
    }

    // Get the oldest message (FIFO)
    msg = rtcData.messages[0];

    // Shift remaining messages down
    for (uint8_t i = 0; i < rtcData.messageCount - 1; i++)
    {
        rtcData.messages[i] = rtcData.messages[i + 1];
    }

    rtcData.messageCount--;

    Serial.print("Message retrieved from RTC memory (");
    Serial.print(rtcData.messageCount);
    Serial.println(" remaining)");

    return true;
}

void SleepManager::clearMessages()
{
    rtcData.messageCount = 0;
    Serial.println("All stored messages cleared from RTC memory");
}

void SleepManager::enterLightSleep()
{
    Serial.println("\n===================================");
    Serial.println("ENTERING LIGHT SLEEP");
    Serial.println("===================================");
    Serial.print("Stored messages: ");
    Serial.println(rtcData.messageCount);
    Serial.print("Wake-up count: ");
    Serial.println(rtcData.wakeupCount);
    Serial.println("Wake-up sources:");
    Serial.print("  - LoRa interrupt on GPIO ");
    Serial.println(loraIntPin);
    Serial.println("===================================\n");

    // Small delay to ensure serial output is flushed
    delay(100);

    // Enter light sleep (instead of deep sleep)
    // Light sleep maintains more system state and wakes faster
    esp_light_sleep_start();
}

String SleepManager::getWakeupReason()
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_EXT1:
        return "Wake-up caused by: LoRa interrupt (EXT1)";
    case ESP_SLEEP_WAKEUP_TIMER:
        return "Wake-up caused by: Timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        return "Wake-up caused by: Touchpad";
    case ESP_SLEEP_WAKEUP_ULP:
        return "Wake-up caused by: ULP program";
    case ESP_SLEEP_WAKEUP_GPIO:
        return "Wake-up caused by: GPIO";
    case ESP_SLEEP_WAKEUP_UART:
        return "Wake-up caused by: UART";
    case ESP_SLEEP_WAKEUP_WIFI:
        return "Wake-up caused by: WiFi";
    case ESP_SLEEP_WAKEUP_COCPU:
        return "Wake-up caused by: COCPU";
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
        return "Wake-up caused by: COCPU trap trigger";
    case ESP_SLEEP_WAKEUP_BT:
        return "Wake-up caused by: Bluetooth";
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
        return "Wake-up caused by: Reset or first boot";
    }
}

bool SleepManager::wasWokenFromSleep()
{
    return esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
}

uint32_t SleepManager::getWakeupCount()
{
    return rtcData.wakeupCount;
}

void SleepManager::initializeRTCData()
{
    rtcData.magic = RTC_MAGIC;
    rtcData.messageCount = 0;
    rtcData.wakeupCount = 0;
    rtcData.lastActivityTime = 0;

    // Clear message buffer
    for (int i = 0; i < MAX_STORED_MESSAGES; i++)
    {
        rtcData.messages[i] = Message();
    }
}

bool SleepManager::isRTCDataValid()
{
    return rtcData.magic == RTC_MAGIC && rtcData.messageCount <= MAX_STORED_MESSAGES;
}
