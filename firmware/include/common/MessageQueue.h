#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

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
 * Templated to support any message type (protobuf or otherwise).
 *
 * Thread-safe and ISR-safe (with appropriate methods).
 */
template <typename T>
class MessageQueue
{
public:
    MessageQueue()
        : queueHandle(nullptr)
    {
        queueHandle = xQueueCreate(MESSAGE_QUEUE_SIZE, sizeof(T));
    }

    ~MessageQueue()
    {
        if (queueHandle != NULL)
        {
            vQueueDelete(queueHandle);
            queueHandle = NULL;
        }
    }

    /**
     * @brief Push a message to the queue (non-blocking)
     * @param msg Message to push
     * @return true if message was added, false if queue is full
     */
    bool push(const T &msg)
    {
        if (queueHandle == NULL) return false;
        return xQueueSend(queueHandle, &msg, 0) == pdTRUE;
    }

    /**
     * @brief Pop a message from the queue (non-blocking)
     * @param msg Reference to store the popped message
     * @return true if message was retrieved, false if queue is empty
     */
    bool pop(T &msg)
    {
        if (queueHandle == NULL) return false;
        return xQueueReceive(queueHandle, &msg, 0) == pdTRUE;
    }

    /**
     * @brief Check if queue is empty
     * @return true if queue has no messages
     */
    bool isEmpty() const
    {
        if (queueHandle == NULL) return true;
        return uxQueueMessagesWaiting(queueHandle) == 0;
    }

    /**
     * @brief Get current number of messages in queue
     * @return Number of messages currently in queue
     */
    int getCount() const
    {
        if (queueHandle == NULL) return 0;
        return (int)uxQueueMessagesWaiting(queueHandle);
    }

private:
    QueueHandle_t queueHandle;
};

#endif // MESSAGE_QUEUE_H
