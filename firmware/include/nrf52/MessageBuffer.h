#ifndef MESSAGE_BUFFER_H
#define MESSAGE_BUFFER_H

#include <common/FirmwareConfig.h>
#include "meshtastic/mesh.pb.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

/**
 * @file MessageBuffer.h (nRF52)
 * @brief LittleFS-backed circular buffer for FromRadio messages that persists across reboots
 *
 * Stores outbound Meshtastic FromRadio messages in LittleFS while BLE is disconnected.
 * Uses protobuf serialization and drop-oldest policy when full.
 */

class MessageBuffer
{
public:
    static constexpr const char *BUFFER_DIR = "/msgbuf";

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
    int m_head;
    int m_tail;
    int m_count;
    bool m_initialized;

    void scanBuffer();
};

#endif // MESSAGE_BUFFER_H
