#ifndef MESSAGE_BUFFER_H
#define MESSAGE_BUFFER_H

#include "Protocol.h"

/**
 * @file MessageBuffer.h
 * @brief Simple fixed-size circular buffer for Message objects.
 *
 * The MessageBuffer is used to hold outbound LoRa messages while a BLE
 * connection is not available. It has a small fixed capacity and uses
 * a drop-oldest policy when full to avoid unbounded memory growth on the
 * embedded device.
 */

class MessageBuffer
{
public:
    /**
     * @brief Create an empty MessageBuffer.
     */
    MessageBuffer() : head(0), tail(0), count(0) {}

    /**
     * @brief Add a message to the buffer.
     *
     * If the buffer is full the oldest message is overwritten (drop-oldest).
     * This makes the buffer suitable for best-effort telemetry where the most
     * recent messages are preferred.
     *
     * @param msg Message to add (copied into internal storage)
     */
    void add(const Message &msg)
    {
        if (count < MAX_MESSAGES)
        {
            buffer[tail] = msg;
            tail = (tail + 1) % MAX_MESSAGES;
            count++;
        }
        else
        {
            // Buffer full - overwrite oldest message
            buffer[tail] = msg;
            tail = (tail + 1) % MAX_MESSAGES;
            head = (head + 1) % MAX_MESSAGES;
        }
    }

    /**
     * @brief Retrieve the next (oldest) message from the buffer.
     *
     * @param[out] msg Destination reference where the message will be copied.
     * @return true if a message was returned, false if buffer was empty.
     */
    bool get(Message &msg)
    {
        if (count == 0)
        {
            return false;
        }

        msg = buffer[head];
        head = (head + 1) % MAX_MESSAGES;
        count--;
        return true;
    }

    /**
     * @brief Number of messages currently stored.
     * @return int Count of messages (0..MAX_MESSAGES)
     */
    int getCount() const
    {
        return count;
    }

    /**
     * @brief True if the buffer contains no messages.
     */
    bool isEmpty() const
    {
        return count == 0;
    }

    /**
     * @brief Remove all messages from the buffer.
     */
    void clear()
    {
        head = 0;
        tail = 0;
        count = 0;
    }

private:
    static const int MAX_MESSAGES = 10;
    Message buffer[MAX_MESSAGES];
    int head;  // Index of next message to read
    int tail;  // Index of next write position
    int count; // Number of messages stored
};

#endif // MESSAGE_BUFFER_H
