#ifndef MESSAGE_BUFFER_H
#define MESSAGE_BUFFER_H

#include <common/Protocol.h>
#include <common/FirmwareConfig.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

/**
 * @file MessageBuffer.h
 * @brief LittleFS-backed circular buffer for Message objects that persists across reboots
 *
 * The MessageBuffer stores outbound LoRa messages in LittleFS (flash filesystem)
 * while BLE is disconnected. Messages survive reboots and power cycles.
 * Uses a drop-oldest policy when full to avoid unbounded storage growth.
 *
 * nRF52 uses LittleFS instead of NVS (ESP32) or FDS (Nordic SDK).
 * LittleFS is built into Adafruit nRF52 core and provides simple file-based storage.
 */

class MessageBuffer
{
public:
    static constexpr const char *BUFFER_DIR = "/msgbuf";

    MessageBuffer();
    ~MessageBuffer();

    /**
     * @brief Initialize LittleFS and load persisted messages
     * Must be called once during setup() before using the buffer
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Add a message to the buffer (persists to flash)
     *
     * If buffer is full, oldest message is overwritten (drop-oldest policy).
     * Message is serialized and stored in LittleFS.
     *
     * @param msg Message to add
     * @return true if message was added successfully
     */
    bool add(const Message &msg);

    /**
     * @brief Peek at the next (oldest) message without removing it
     *
     * Reads message from flash and deserializes it.
     *
     * @param msg Output parameter for the message
     * @return true if a message was retrieved, false if buffer is empty
     */
    bool peek(Message &msg);

    /**
     * @brief Remove the front (oldest) message from the buffer
     *
     * Deletes message from flash and updates buffer state.
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
    bool isFull() const { return m_count >= (int)BufferConstants::MAX_BUFFERED_MESSAGES; }

    /**
     * @brief Clear all messages from buffer and flash
     */
    void clear();

private:
    int m_head;  // Unused in single-file mode (kept for compatibility)
    int m_tail;  // Read offset in buffer file
    int m_count; // Number of messages in buffer
    bool m_initialized;

    /**
     * @brief Scan buffer file on startup to count messages
     * No separate state file needed - derives count from file content
     */
    void scanBuffer();
};

#endif // MESSAGE_BUFFER_H
