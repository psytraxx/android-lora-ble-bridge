#ifndef MESSAGE_BUFFER_H
#define MESSAGE_BUFFER_H

#include "Protocol.h"
#include "FirmwareConfig.h"
#include <nvs_flash.h>
#include <nvs.h>
#include "esp_log.h"

/**
 * @file MessageBuffer.h
 * @brief NVS-backed circular buffer for Message objects that persists across deep sleep
 *
 * The MessageBuffer stores outbound LoRa messages in NVS (Non-Volatile Storage)
 * while BLE is disconnected. Messages survive deep sleep and device resets.
 * Uses a drop-oldest policy when full to avoid unbounded storage growth.
 */

class MessageBuffer
{
public:
    static constexpr size_t MAX_MESSAGES = BufferConstants::MAX_BUFFERED_MESSAGES;
    static constexpr size_t MAX_MESSAGE_SIZE = BufferConstants::MAX_PROTOCOL_MESSAGE;
    static constexpr const char *NVS_NAMESPACE = "msg_buffer";
    static constexpr const char *NVS_KEY_COUNT = "count";
    static constexpr const char *NVS_KEY_HEAD = "head";
    static constexpr const char *NVS_KEY_TAIL = "tail";

    MessageBuffer();
    ~MessageBuffer();

    /**
     * @brief Initialize NVS and load persisted messages
     * Must be called once during setup() before using the buffer
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Add a message to the buffer (persists to NVS)
     * 
     * If buffer is full, oldest message is overwritten (drop-oldest policy).
     * Message is serialized and stored in NVS flash.
     * 
     * @param msg Message to add
     * @return true if message was added successfully
     */
    bool add(const Message &msg);

    /**
     * @brief Peek at the next (oldest) message without removing it
     * 
     * Reads message from NVS and deserializes it.
     * 
     * @param msg Output parameter for the message
     * @return true if a message was retrieved, false if buffer is empty
     */
    bool peek(Message &msg);

    /**
     * @brief Remove the front (oldest) message from the buffer
     * 
     * Deletes message from NVS and updates buffer state.
     * Use after peek() for reliable message processing.
     * 
     * @return true if a message was removed, false if buffer is empty
     */
    bool popFront();

    /**
     * @brief Get number of messages in buffer
     */
    int getCount() const { return m_count; }

    /**
     * @brief Check if buffer is empty
     */
    bool isEmpty() const { return m_count == 0; }

    /**
     * @brief Check if buffer is full
     */
    bool isFull() const { return m_count >= (int)MAX_MESSAGES; }

    /**
     * @brief Clear all messages from buffer and NVS
     */
    void clear();

private:
    nvs_handle_t m_nvsHandle;
    int m_head;  // Index where next message will be written
    int m_tail;  // Index of next message to read
    int m_count; // Number of messages in buffer
    bool m_initialized;

    static const char *TAG;

    /**
     * @brief Load buffer state from NVS (head, tail, count)
     */
    void loadState();

    /**
     * @brief Save buffer state to NVS (head, tail, count)
     */
    void saveState();

    /**
     * @brief Generate NVS key for message at index
     */
    void getMessageKey(size_t index, char *keyBuf, size_t keyBufSize);
};

#endif // MESSAGE_BUFFER_H
