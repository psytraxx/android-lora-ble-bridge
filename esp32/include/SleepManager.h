#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include <Arduino.h>
#include "Protocol.h"
#include <esp_sleep.h>

// Light sleep timeout in milliseconds (2 minutes)
#define LIGHT_SLEEP_TIMEOUT_MS (2 * 60 * 1000)

// Maximum number of messages that can be stored in RTC memory
#define MAX_STORED_MESSAGES 10

// Structure stored in RTC memory that persists across light sleep
typedef struct
{
    uint32_t magic;                        // Magic number to verify valid data
    uint8_t messageCount;                  // Number of stored messages
    Message messages[MAX_STORED_MESSAGES]; // Message buffer
    uint32_t wakeupCount;                  // Count of wake-ups (for debugging)
    uint32_t lastActivityTime;             // Last activity timestamp
} RTC_DATA_ATTR SleepData;

class SleepManager
{
public:
    SleepManager(int loraInterruptPin);

    /**
     * @brief Initialize the sleep manager and configure wake-up sources
     */
    void setup();

    /**
     * @brief Update last activity timestamp
     */
    void updateActivity();

    /**
     * @brief Check if it's time to enter light sleep
     * @return true if should enter light sleep
     */
    bool shouldEnterLightSleep();

    /**
     * @brief Store a message in RTC memory for later delivery
     * @param msg Message to store
     * @return true if stored successfully, false if buffer full
     */
    bool storeMessage(const Message &msg);

    /**
     * @brief Get the number of stored messages
     * @return Number of messages in RTC memory
     */
    uint8_t getStoredMessageCount();

    /**
     * @brief Retrieve and remove the oldest stored message
     * @param msg Output parameter to receive the message
     * @return true if a message was retrieved, false if no messages stored
     */
    bool retrieveMessage(Message &msg);

    /**
     * @brief Clear all stored messages
     */
    void clearMessages();

    /**
     * @brief Enter light sleep mode
     * Will configure wake sources and print debug info before sleeping
     */
    void enterLightSleep();

    /**
     * @brief Get wake-up reason after boot
     * @return String describing the wake-up reason
     */
    String getWakeupReason();

    /**
     * @brief Check if this boot was from light sleep
     * @return true if woken from light sleep
     */
    bool wasWokenFromSleep();

    /**
     * @brief Get wake-up count (how many times woken from sleep)
     * @return Wake-up count
     */
    uint32_t getWakeupCount();

private:
    int loraIntPin;
    unsigned long lastActivityMillis;

    /**
     * @brief Initialize RTC memory data structure
     */
    void initializeRTCData();

    /**
     * @brief Verify RTC data is valid
     * @return true if valid
     */
    bool isRTCDataValid();
};

#endif // SLEEP_MANAGER_H
