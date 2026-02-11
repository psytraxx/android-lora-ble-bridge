#ifndef MESSAGE_BUFFER_H
#define MESSAGE_BUFFER_H

#include <common/FirmwareConfig.h>
#include "meshtastic/mesh.pb.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <nvs_flash.h>
#include <nvs.h>

/**
 * @file MessageBuffer.h (ESP32)
 * @brief NVS-backed circular buffer for FromRadio messages that persists across deep sleep
 *
 * Stores outbound Meshtastic FromRadio messages in NVS while BLE is disconnected.
 * Uses protobuf serialization and drop-oldest policy when full.
 */

class MessageBuffer
{
public:
    static constexpr const char *NVS_NAMESPACE = "msg_buffer";
    static constexpr const char *NVS_KEY_COUNT = "count";
    static constexpr const char *NVS_KEY_HEAD = "head";
    static constexpr const char *NVS_KEY_TAIL = "tail";

    MessageBuffer();
    ~MessageBuffer();

    bool begin();
    bool add(const meshtastic_FromRadio &msg);
    bool peek(meshtastic_FromRadio &msg);
    bool popFront();
    int getCount() const { return m_count; }
    bool isEmpty() const { return m_count == 0; }
    bool isFull() const { return m_count >= (int)BufferConstants::MAX_BUFFERED_MESSAGES; }
    void clear();

private:
    nvs_handle_t m_nvsHandle;
    int m_head;
    int m_tail;
    int m_count;
    bool m_initialized;

    void loadState();
    void saveState();
    void getMessageKey(size_t index, char *keyBuf, size_t keyBufSize);
};

#endif // MESSAGE_BUFFER_H
