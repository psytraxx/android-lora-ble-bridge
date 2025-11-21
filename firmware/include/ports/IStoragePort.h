#ifndef ISTORAGE_PORT_H
#define ISTORAGE_PORT_H

#include "Protocol.h"

/**
 * @brief Storage Port Interface (Hexagonal Architecture)
 *
 * Abstracts persistent message storage across different platforms:
 * - ESP32: NVS (Non-Volatile Storage)
 * - nRF52: LittleFS (Internal Flash File System)
 */
class IStoragePort
{
public:
    virtual ~IStoragePort() = default;

    /**
     * @brief Initialize storage backend
     * @return true on success, false on failure
     */
    virtual bool begin() = 0;

    /**
     * @brief Add a message to storage
     * @param msg The message to store
     * @return true on success, false on failure
     */
    virtual bool add(const Message &msg) = 0;

    /**
     * @brief Peek at the front message without removing it
     * @param msg Output parameter for the message
     * @return true if message retrieved, false if queue empty
     */
    virtual bool peek(Message &msg) = 0;

    /**
     * @brief Remove the front message from storage
     * @return true on success, false on failure
     */
    virtual bool popFront() = 0;

    /**
     * @brief Check if storage is empty
     * @return true if empty, false otherwise
     */
    virtual bool isEmpty() = 0;

    /**
     * @brief Get the number of stored messages
     * @return Message count
     */
    virtual int getCount() = 0;
};

#endif // ISTORAGE_PORT_H
