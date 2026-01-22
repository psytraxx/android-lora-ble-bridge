#include "nrf52/MessageBuffer.h"
#include "common/Logging.h"
#include <Arduino.h>

static const char *TAG = "MsgBuf";

// Single file for all buffered messages (no separate state file needed)
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
    LOG_I(TAG, "Initializing MessageBuffer with LittleFS (single-file mode)");

    // Initialize Internal File System
    InternalFS.begin();

    // Create buffer directory if it doesn't exist
    if (!InternalFS.exists(BUFFER_DIR))
    {
        LOG_I(TAG, "Creating message buffer directory");
        InternalFS.mkdir(BUFFER_DIR);
    }

    // Scan buffer file to determine state (no separate state file needed)
    scanBuffer();

    m_initialized = true;
    LOG_I(TAG, "MessageBuffer initialized: %d messages in buffer", m_count);

    return true;
}

bool MessageBuffer::add(const Message &msg)
{
    if (!m_initialized)
    {
        LOG_E(TAG, "MessageBuffer not initialized");
        return false;
    }

    // Check if buffer is full
    if (isFull())
    {
        LOG_W(TAG, "Buffer full (%d messages), dropping oldest", m_count);
        popFront();
    }

    // Serialize message
    uint8_t buffer[BufferConstants::MAX_PROTOCOL_MESSAGE];
    int len = msg.serialize(buffer, sizeof(buffer));

    if (len <= 0)
    {
        LOG_E(TAG, "Failed to serialize message for buffering");
        return false;
    }

    // Open buffer file in append mode
    File file = InternalFS.open(BUFFER_FILE, FILE_O_WRITE);
    if (!file)
    {
        LOG_E(TAG, "Failed to open buffer file for append");
        return false;
    }

    // Seek to end for append (LittleFS doesn't support SEEK_END)
    size_t fileSize = file.size();
    if (!file.seek(fileSize))
    {
        LOG_E(TAG, "Failed to seek to end of buffer file");
        file.close();
        return false;
    }

    // Write message: [2-byte length][payload]
    uint16_t msgLen = (uint16_t)len;
    size_t written = 0;
    written += file.write((uint8_t *)&msgLen, sizeof(msgLen));
    written += file.write(buffer, len);
    file.close();

    if (written != (sizeof(msgLen) + len))
    {
        LOG_E(TAG, "Failed to write complete message to buffer");
        return false;
    }

    // Update count
    m_count++;

    LOG_I(TAG, "Message buffered (%d bytes, total: %d messages)", len, m_count);

    return true;
}

bool MessageBuffer::peek(Message &msg)
{
    if (!m_initialized || isEmpty())
    {
        return false;
    }

    // Open buffer file
    File file = InternalFS.open(BUFFER_FILE, FILE_O_READ);
    if (!file)
    {
        LOG_E(TAG, "Failed to open buffer file for reading");
        return false;
    }

    // Seek to current read position
    if (!file.seek(m_tail))
    {
        LOG_E(TAG, "Failed to seek to read position %d", m_tail);
        file.close();
        return false;
    }

    // Read message length
    uint16_t msgLen = 0;
    if (file.read((uint8_t *)&msgLen, sizeof(msgLen)) != sizeof(msgLen))
    {
        LOG_E(TAG, "Failed to read message length at offset %d", m_tail);
        file.close();
        // Corruption - clear buffer
        clear();
        return false;
    }

    // Validate length
    if (msgLen == 0 || msgLen > BufferConstants::MAX_PROTOCOL_MESSAGE)
    {
        LOG_E(TAG, "Invalid message length %u at offset %d - clearing buffer", msgLen, m_tail);
        file.close();
        clear();
        return false;
    }

    // Read message payload
    uint8_t buffer[BufferConstants::MAX_PROTOCOL_MESSAGE];
    if (file.read(buffer, msgLen) != msgLen)
    {
        LOG_E(TAG, "Failed to read complete message payload");
        file.close();
        clear();
        return false;
    }
    file.close();

    // Deserialize
    if (!msg.deserialize(buffer, msgLen))
    {
        LOG_E(TAG, "Failed to deserialize message - clearing buffer");
        clear();
        return false;
    }

    return true;
}

bool MessageBuffer::popFront()
{
    if (isEmpty() || !m_initialized)
    {
        return false;
    }

    // Read current message length to advance read pointer
    File file = InternalFS.open(BUFFER_FILE, FILE_O_READ);
    if (!file)
    {
        LOG_E(TAG, "Failed to open buffer file to pop");
        return false;
    }

    // Seek to current read position
    if (!file.seek(m_tail))
    {
        file.close();
        return false;
    }

    // Read message length
    uint16_t msgLen = 0;
    if (file.read((uint8_t *)&msgLen, sizeof(msgLen)) != sizeof(msgLen))
    {
        LOG_E(TAG, "Failed to read message length for pop");
        file.close();
        return false;
    }
    file.close();

    // Advance read pointer past this message (length header + payload)
    m_tail += sizeof(uint16_t) + msgLen;
    m_count--;

    // If buffer is now empty, compact/reset the file
    if (m_count == 0)
    {
        LOG_I(TAG, "Buffer empty, compacting file");
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
    if (!m_initialized)
    {
        return;
    }

    LOG_I(TAG, "Clearing message buffer");

    // Delete buffer file
    if (InternalFS.exists(BUFFER_FILE))
    {
        InternalFS.remove(BUFFER_FILE);
    }

    // Reset state
    m_tail = 0;
    m_count = 0;
}

void MessageBuffer::scanBuffer()
{
    m_tail = 0;
    m_count = 0;

    // No buffer file = empty buffer
    if (!InternalFS.exists(BUFFER_FILE))
    {
        LOG_I(TAG, "No buffer file, starting fresh");
        return;
    }

    // Scan buffer file to count messages
    File file = InternalFS.open(BUFFER_FILE, FILE_O_READ);
    if (!file)
    {
        LOG_E(TAG, "Failed to open buffer file for scanning");
        return;
    }

    size_t fileSize = file.size();
    size_t offset = 0;
    int msgCount = 0;

    while (offset < fileSize && msgCount < (int)BufferConstants::MAX_BUFFERED_MESSAGES)
    {
        // Read message length
        if (!file.seek(offset))
        {
            LOG_W(TAG, "Seek failed at offset %u, stopping scan", offset);
            break;
        }

        uint16_t msgLen = 0;
        if (file.read((uint8_t *)&msgLen, sizeof(msgLen)) != sizeof(msgLen))
        {
            LOG_W(TAG, "Failed to read length at offset %u, file may be truncated", offset);
            break;
        }

        // Validate length
        if (msgLen == 0 || msgLen > BufferConstants::MAX_PROTOCOL_MESSAGE)
        {
            LOG_E(TAG, "Invalid message length %u at offset %u - buffer corrupted", msgLen, offset);
            file.close();
            clear();
            return;
        }

        // Skip message payload
        offset += sizeof(uint16_t) + msgLen;
        msgCount++;
    }

    file.close();

    m_count = msgCount;
    m_tail = 0; // Always read from start

    LOG_I(TAG, "Scanned buffer: %d messages, %u bytes total", m_count, fileSize);
}
