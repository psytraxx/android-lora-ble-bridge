#include "SleepManager.h"

// Magic number to identify valid RTC data
#define RTC_MAGIC 0xDEADBEEF

// RTC memory data - persists across deep sleep
RTC_DATA_ATTR SleepData rtcData;

SleepManager::SleepManager(int loraInterruptPin)
    : loraIntPin(loraInterruptPin), lastActivityMillis(0)
{
}

void SleepManager::setup()
{
    Serial.println("Initializing Sleep Manager...");

    // Check if we're waking from deep sleep
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED)
    {
        // First boot - initialize RTC data
        Serial.println("First boot - initializing RTC memory");
        initializeRTCData();
    }
    else
    {
        // Waking from deep sleep
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

    // Configure wake-up buttons (active LOW - pressed = LOW)
    pinMode(WAKE_BUTTON_PIN_1, INPUT_PULLUP);
    pinMode(WAKE_BUTTON_PIN_2, INPUT_PULLUP);

    // Configure LoRa interrupt pin for wake-up (active HIGH when packet received)
    pinMode(loraIntPin, INPUT);

    // Use EXT1 for multiple wake-up sources (buttons + LoRa)
    // EXT1 can wake on ANY_HIGH or ALL_LOW for multiple GPIOs
    // Buttons are active LOW, LoRa interrupt is active HIGH
    // We need to wake on: Button1 LOW OR Button2 LOW OR LoRa HIGH

    // Strategy: Use EXT1 with ALL_LOW mode for buttons, and separate handling for LoRa
    // Or use a combination - wake on button press (LOW) or LoRa (HIGH)

    // Since we need mixed polarity (buttons LOW, LoRa HIGH), we'll use two wake sources:
    // - EXT0 for one button (GPIO 0)
    // - EXT1 for second button (GPIO 15) and LoRa (GPIO 32)

    // Configure EXT0 for Button 1 (GPIO 0) - wake on LOW
    esp_sleep_enable_ext0_wakeup(WAKE_BUTTON_PIN_1, 0);

    // Configure EXT1 for Button 2 (GPIO 15) and LoRa (GPIO 32)
    // Wake when either goes to their active state
    // Note: EXT1 ALL_LOW means wake when ALL pins are LOW (AND logic)
    // Note: EXT1 ANY_HIGH means wake when ANY pin is HIGH (OR logic)
    // Since GPIO 15 button is active LOW and needs pull-up, we need to invert logic

    // Best approach: Use EXT1 for LoRa interrupt only (ANY_HIGH)
    // And handle second button with a workaround or use timer
    uint64_t ext1_mask = (1ULL << WAKE_BUTTON_PIN_2) | (1ULL << loraIntPin);

    // For mixed polarity, we'll wake on ANY_HIGH
    // Button 2 (GPIO 15) with pull-up will be HIGH when not pressed, LOW when pressed
    // We'll need to invert its logic or accept it wakes when released
    // Better: just wake on LoRa interrupt via EXT1
    esp_sleep_enable_ext1_wakeup(1ULL << loraIntPin, ESP_EXT1_WAKEUP_ANY_HIGH);

    // For the second button, we can add it to the mask with ALL_LOW mode
    // But this requires both to be pressed. Instead, let's document this limitation.
    // Alternative: Use GPIO wakeup which supports individual pin config (ESP32-S2/S3/C3)

    // Workaround for classic ESP32: Add second button to EXT1 with inverted logic
    // When button is pressed (LOW), we need to detect it
    // We can use ALL_LOW mode, but that means ALL pins must be LOW
    // This won't work well with LoRa needing HIGH

    // Practical solution: Use EXT0 for button 1, and add button 2 to EXT1 mask
    // Accept that button 2 needs to be pressed AND released to wake (due to pull-up)
    // Or reconfigure button 2 as active HIGH with external pull-down

    // For now, let's add button 2 with ANY_HIGH mode (will wake when released)
    ext1_mask = (1ULL << WAKE_BUTTON_PIN_2) | (1ULL << loraIntPin);
    esp_sleep_enable_ext1_wakeup(ext1_mask, ESP_EXT1_WAKEUP_ANY_HIGH);

    Serial.println("Sleep wake-up sources configured:");
    Serial.print("  - Button 1 (BOOT) on GPIO ");
    Serial.print(WAKE_BUTTON_PIN_1);
    Serial.println(" (press to wake)");
    Serial.print("  - Button 2 (EN) on GPIO ");
    Serial.print(WAKE_BUTTON_PIN_2);
    Serial.println(" (release to wake - due to pull-up)");
    Serial.print("  - LoRa interrupt on GPIO ");
    Serial.println(loraIntPin);

    // Initialize activity timer
    updateActivity();
}

void SleepManager::updateActivity()
{
    lastActivityMillis = millis();
}

bool SleepManager::shouldEnterDeepSleep()
{
    unsigned long currentMillis = millis();
    unsigned long inactiveTime = currentMillis - lastActivityMillis;

    return inactiveTime >= DEEP_SLEEP_TIMEOUT_MS;
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

void SleepManager::enterDeepSleep()
{
    Serial.println("\n===================================");
    Serial.println("ENTERING DEEP SLEEP");
    Serial.println("===================================");
    Serial.print("Stored messages: ");
    Serial.println(rtcData.messageCount);
    Serial.print("Wake-up count: ");
    Serial.println(rtcData.wakeupCount);
    Serial.println("Wake-up sources:");
    Serial.print("  - Button 1 (BOOT) on GPIO ");
    Serial.println(WAKE_BUTTON_PIN_1);
    Serial.print("  - Button 2 (EN) on GPIO ");
    Serial.println(WAKE_BUTTON_PIN_2);
    Serial.print("  - LoRa interrupt on GPIO ");
    Serial.println(loraIntPin);
    Serial.println("===================================\n");

    // Small delay to ensure serial output is flushed
    delay(100);

    // Enter deep sleep
    esp_deep_sleep_start();
}

String SleepManager::getWakeupReason()
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_EXT0:
        return "Wake-up caused by: Button press (EXT0)";
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
