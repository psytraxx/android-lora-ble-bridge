#ifndef ESP32_STORAGE_ADAPTER_H
#define ESP32_STORAGE_ADAPTER_H

#include "ports/IStoragePort.h"
#include "esp32/MessageBuffer.h"

/**
 * @brief ESP32 Storage Adapter (NVS-based)
 *
 * Wraps MessageBuffer which uses ESP32 NVS for persistent storage
 */
class ESP32StorageAdapter : public IStoragePort
{
public:
    ESP32StorageAdapter() : messageBuffer() {}

    bool begin() override
    {
        return messageBuffer.begin();
    }

    bool add(const Message &msg) override
    {
        return messageBuffer.add(msg);
    }

    bool peek(Message &msg) override
    {
        return messageBuffer.peek(msg);
    }

    bool popFront() override
    {
        return messageBuffer.popFront();
    }

    bool isEmpty() override
    {
        return messageBuffer.isEmpty();
    }

    int getCount() override
    {
        return messageBuffer.getCount();
    }

private:
    MessageBuffer messageBuffer;
};

#endif // ESP32_STORAGE_ADAPTER_H
