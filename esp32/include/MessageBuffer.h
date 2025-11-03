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
        if (count < BufferConstants::MAX_BUFFERED_MESSAGES)
        {
            buffer[tail] = msg;
            tail = (tail + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
            count++;
        }
        else
        {
            // Buffer full - overwrite oldest message
            buffer[tail] = msg;
            tail = (tail + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
            head = (head + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
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
        head = (head + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
        count--;
        return true;
    }

    /**
     * @brief Peek at the next (oldest) message without removing it.
     *
     * @param[out] msg Destination reference where the message will be copied.
     * @return true if a message was returned, false if buffer was empty.
     */
    bool peek(Message &msg) const
    {
        if (count == 0)
        {
            return false;
        }

        msg = buffer[head];
        return true;
    }

    /**
     * @brief Remove the front (oldest) message from the buffer.
     *
     * Use this after peek() to implement peek-then-pop pattern for
     * reliable message processing.
     *
     * @return true if a message was removed, false if buffer was empty.
     */
    bool popFront()
    {
        if (count == 0)
        {
            return false;
        }

        head = (head + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
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
    // Buffer capacity: 10 messages provides reasonable headroom for burst traffic
    // Each Message is ~160 bytes, so 10 messages = ~1.6 KB RAM
    // Drop-oldest policy when full prevents unbounded memory growth
    Message buffer[BufferConstants::MAX_BUFFERED_MESSAGES];
    int head;  // Index of next message to read
    int tail;  // Index of next write position
    int count; // Number of messages stored
};

#endif // MESSAGE_BUFFER_H
