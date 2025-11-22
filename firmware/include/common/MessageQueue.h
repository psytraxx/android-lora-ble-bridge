#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <common/Protocol.h>

// Platform-specific FreeRTOS includes
#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#elif defined(ARDUINO_ARCH_NRF52)
#include <FreeRTOS.h>
#include <queue.h>
#else
#error "Unsupported platform"
#endif

// Queue size constant
#ifndef MESSAGE_QUEUE_SIZE
#define MESSAGE_QUEUE_SIZE 10
#endif

/**
 * @brief Thread-safe message queue using FreeRTOS queues
 *
 * Provides a platform-agnostic wrapper around FreeRTOS queue API.
 * Used for in-memory message queuing between BLE and LoRa.
 *
 * Thread-safe and ISR-safe (with appropriate methods).
 * Replaces the previous circular buffer implementation with FreeRTOS queues
 * for better thread safety and consistency across platforms.
 */
class MessageQueue
{
public:
    /**
     * @brief Construct a new Message Queue
     * Creates FreeRTOS queue with MESSAGE_QUEUE_SIZE capacity
     */
    MessageQueue();

    /**
     * @brief Destroy the Message Queue
     * Deletes the FreeRTOS queue handle
     */
    ~MessageQueue();

    /**
     * @brief Push a message to the queue (non-blocking)
     * @param msg Message to push
     * @return true if message was added, false if queue is full
     */
    bool push(const Message &msg);

    /**
     * @brief Pop a message from the queue (non-blocking)
     * @param msg Reference to store the popped message
     * @return true if message was retrieved, false if queue is empty
     */
    bool pop(Message &msg);

    /**
     * @brief Check if queue is empty
     * @return true if queue has no messages, false otherwise
     */
    bool isEmpty() const;

    /**
     * @brief Get current number of messages in queue
     * @return Number of messages currently in queue
     */
    int getCount() const;

private:
    QueueHandle_t queueHandle;
};

#endif // MESSAGE_QUEUE_H
