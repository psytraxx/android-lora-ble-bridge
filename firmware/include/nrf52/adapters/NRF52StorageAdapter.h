#ifndef NRF52_STORAGE_ADAPTER_H
#define NRF52_STORAGE_ADAPTER_H

#include "ports/IStoragePort.h"
#include "nrf52/MessageBuffer.h"

/**
 * @brief nRF52 Storage Adapter (LittleFS-based)
 *
 * Wraps MessageBuffer which uses LittleFS for persistent storage
 */
class NRF52StorageAdapter : public IStoragePort
{
public:
    NRF52StorageAdapter() : messageBuffer() {}

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

#endif // NRF52_STORAGE_ADAPTER_H
