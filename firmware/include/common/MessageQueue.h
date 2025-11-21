#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include "Protocol.h"

// Queue size constant (previously in FirmwareConfig.h)
#ifndef MESSAGE_QUEUE_SIZE
#define MESSAGE_QUEUE_SIZE 10
#endif

/**
 * @brief Simple circular buffer message queue (platform-agnostic)
 *
 * Used for in-memory message queuing between BLE and LoRa.
 * This is the same implementation previously duplicated in:
 * - firmware/src/esp32/main.cpp
 * - firmware/include/nrf52/BLEManager.h
 */
struct MessageQueue
{
    Message messages[MESSAGE_QUEUE_SIZE];
    int head;
    int tail;
    int count;

    MessageQueue() : head(0), tail(0), count(0) {}

    bool push(const Message &msg)
    {
        if (count >= MESSAGE_QUEUE_SIZE)
            return false;
        messages[tail] = msg;
        tail = (tail + 1) % MESSAGE_QUEUE_SIZE;
        count++;
        return true;
    }

    bool pop(Message &msg)
    {
        if (count == 0)
            return false;
        msg = messages[head];
        head = (head + 1) % MESSAGE_QUEUE_SIZE;
        count--;
        return true;
    }

    bool isEmpty() const { return count == 0; }
};

#endif // MESSAGE_QUEUE_H
