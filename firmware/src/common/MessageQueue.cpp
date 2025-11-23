#include "common/MessageQueue.h"
#include "common/Logging.h"
#include <Arduino.h>

static const char* TAG = "MsgQ";

MessageQueue::MessageQueue()
    : queueHandle(nullptr)
{
    // Create FreeRTOS queue for Message objects
    queueHandle = xQueueCreate(MESSAGE_QUEUE_SIZE, sizeof(Message));

    if (queueHandle == NULL)
    {
        LOG_E(TAG, "Failed to create message queue!");
    }
}

MessageQueue::~MessageQueue()
{
    if (queueHandle != NULL)
    {
        vQueueDelete(queueHandle);
        queueHandle = NULL;
    }
}

bool MessageQueue::push(const Message &msg)
{
    if (queueHandle == NULL)
    {
        return false;
    }

    // Non-blocking send (timeout = 0)
    // Returns pdTRUE if successful, pdFALSE if queue is full
    return xQueueSend(queueHandle, &msg, 0) == pdTRUE;
}

bool MessageQueue::pop(Message &msg)
{
    if (queueHandle == NULL)
    {
        return false;
    }

    // Non-blocking receive (timeout = 0)
    // Returns pdTRUE if successful, pdFALSE if queue is empty
    return xQueueReceive(queueHandle, &msg, 0) == pdTRUE;
}

bool MessageQueue::isEmpty() const
{
    if (queueHandle == NULL)
    {
        return true;
    }

    return uxQueueMessagesWaiting(queueHandle) == 0;
}

int MessageQueue::getCount() const
{
    if (queueHandle == NULL)
    {
        return 0;
    }

    return (int)uxQueueMessagesWaiting(queueHandle);
}
