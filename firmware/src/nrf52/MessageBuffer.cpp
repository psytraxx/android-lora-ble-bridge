#include "nrf52/MessageBuffer.h"
#include "common/Logging.h"
#include <Arduino.h>

static const char *TAG = "MsgBuf";

#define BUFFER_FILE "/msgbuf/buf.dat"

MessageBuffer::MessageBuffer()
    : m_head(0), m_tail(0), m_count(0), m_initialized(false)
{
}

MessageBuffer::~MessageBuffer()
{
}

bool MessageBuffer::begin()
{
    LOG_I(TAG, "Initializing MessageBuffer with LittleFS");

    InternalFS.begin();

    if (!InternalFS.exists(BUFFER_DIR))
    {
        InternalFS.mkdir(BUFFER_DIR);
    }

    scanBuffer();

    m_initialized = true;
    LOG_I(TAG, "MessageBuffer initialized: %d messages in buffer", m_count);
    return true;
}

bool MessageBuffer::add(const meshtastic_FromRadio &msg)
{
    if (!m_initialized)
    {
        LOG_E(TAG, "MessageBuffer not initialized");
        return false;
    }

    if (isFull())
    {
        LOG_W(TAG, "Buffer full, dropping oldest");
        popFront();
    }

    // Serialize protobuf
    uint8_t buffer[MeshtasticBLE::MAX_TO_FROM_RADIO_SIZE];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    if (!pb_encode(&stream, meshtastic_FromRadio_fields, &msg))
    {
        LOG_E(TAG, "Failed to serialize FromRadio for buffering");
        return false;
    }
    size_t len = stream.bytes_written;

    File file = InternalFS.open(BUFFER_FILE, FILE_O_WRITE);
    if (!file)
    {
        LOG_E(TAG, "Failed to open buffer file");
        return false;
    }

    size_t fileSize = file.size();
    if (!file.seek(fileSize))
    {
        file.close();
        return false;
    }

    uint16_t msgLen = (uint16_t)len;
    size_t written = 0;
    written += file.write((uint8_t *)&msgLen, sizeof(msgLen));
    written += file.write(buffer, len);
    file.close();

    if (written != (sizeof(msgLen) + len))
    {
        LOG_E(TAG, "Incomplete write to buffer");
        return false;
    }

    m_count++;
    LOG_I(TAG, "Message buffered (%d bytes, total: %d)", len, m_count);
    return true;
}

bool MessageBuffer::peek(meshtastic_FromRadio &msg)
{
    if (!m_initialized || isEmpty()) return false;

    File file = InternalFS.open(BUFFER_FILE, FILE_O_READ);
    if (!file)
    {
        LOG_E(TAG, "Failed to open buffer file");
        return false;
    }

    if (!file.seek(m_tail))
    {
        file.close();
        return false;
    }

    uint16_t msgLen = 0;
    if (file.read((uint8_t *)&msgLen, sizeof(msgLen)) != sizeof(msgLen))
    {
        file.close();
        clear();
        return false;
    }

    if (msgLen == 0 || msgLen > MeshtasticBLE::MAX_TO_FROM_RADIO_SIZE)
    {
        file.close();
        clear();
        return false;
    }

    uint8_t buffer[MeshtasticBLE::MAX_TO_FROM_RADIO_SIZE];
    if (file.read(buffer, msgLen) != msgLen)
    {
        file.close();
        clear();
        return false;
    }
    file.close();

    // Deserialize protobuf
    msg = meshtastic_FromRadio_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(buffer, msgLen);
    if (!pb_decode(&stream, meshtastic_FromRadio_fields, &msg))
    {
        LOG_W(TAG, "Failed to deserialize FromRadio, clearing buffer");
        clear();
        return false;
    }

    return true;
}

bool MessageBuffer::popFront()
{
    if (isEmpty() || !m_initialized) return false;

    File file = InternalFS.open(BUFFER_FILE, FILE_O_READ);
    if (!file) return false;

    if (!file.seek(m_tail))
    {
        file.close();
        return false;
    }

    uint16_t msgLen = 0;
    if (file.read((uint8_t *)&msgLen, sizeof(msgLen)) != sizeof(msgLen))
    {
        file.close();
        return false;
    }
    file.close();

    m_tail += sizeof(uint16_t) + msgLen;
    m_count--;

    if (m_count == 0)
    {
        if (InternalFS.exists(BUFFER_FILE))
        {
            InternalFS.remove(BUFFER_FILE);
        }
        m_tail = 0;
    }

    LOG_I(TAG, "Message popped, %d remaining", m_count);
    return true;
}

void MessageBuffer::clear()
{
    if (!m_initialized) return;

    if (InternalFS.exists(BUFFER_FILE))
    {
        InternalFS.remove(BUFFER_FILE);
    }

    m_tail = 0;
    m_count = 0;
    LOG_I(TAG, "MessageBuffer cleared");
}

void MessageBuffer::scanBuffer()
{
    m_tail = 0;
    m_count = 0;

    if (!InternalFS.exists(BUFFER_FILE))
    {
        return;
    }

    File file = InternalFS.open(BUFFER_FILE, FILE_O_READ);
    if (!file) return;

    size_t fileSize = file.size();
    size_t offset = 0;
    int msgCount = 0;

    while (offset < fileSize && msgCount < (int)BufferConstants::MAX_BUFFERED_MESSAGES)
    {
        if (!file.seek(offset)) break;

        uint16_t msgLen = 0;
        if (file.read((uint8_t *)&msgLen, sizeof(msgLen)) != sizeof(msgLen)) break;

        if (msgLen == 0 || msgLen > MeshtasticBLE::MAX_TO_FROM_RADIO_SIZE)
        {
            file.close();
            clear();
            return;
        }

        offset += sizeof(uint16_t) + msgLen;
        msgCount++;
    }

    file.close();
    m_count = msgCount;
    LOG_I(TAG, "Scanned buffer: %d messages", m_count);
}
