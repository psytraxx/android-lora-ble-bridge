#ifndef MESSAGE_BUFFER_H
#define MESSAGE_BUFFER_H

#include "Protocol.h"

/**
 * Circular buffer for storing LoRa messages when BLE is disconnected
 * Holds up to 10 messages, drops oldest when full
 */
class MessageBuffer
{
public:
    MessageBuffer() : head(0), tail(0), count(0) {}

    /**
     * Add a message to the buffer
     * Drops oldest message if buffer is full
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
            // Buffer full - drop oldest message
            buffer[tail] = msg;
            tail = (tail + 1) % MAX_MESSAGES;
            head = (head + 1) % MAX_MESSAGES;
        }
    }

    /**
     * Get next message from buffer
     * Returns true if message retrieved, false if buffer empty
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
     * Get number of messages in buffer
     */
    int getCount() const
    {
        return count;
    }

    /**
     * Check if buffer is empty
     */
    bool isEmpty() const
    {
        return count == 0;
    }

    /**
     * Clear all messages from buffer
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
    int head; // Next message to read
    int tail; // Next position to write
    int count; // Number of messages in buffer
};

#endif // MESSAGE_BUFFER_H
