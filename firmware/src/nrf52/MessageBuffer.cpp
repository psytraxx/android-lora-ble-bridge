#include "nrf52/MessageBuffer.h"
#include "common/Logging.h"
#include <Arduino.h>

static const char *TAG = "MsgBuf";

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

    // Initialize Internal File System
    InternalFS.begin();

    // Create buffer directory if it doesn't exist
    if (!InternalFS.exists(BUFFER_DIR))
    {
        LOG_I(TAG, "Creating message buffer directory");
        InternalFS.mkdir(BUFFER_DIR);
    }

    // Load state from flash
    loadState();

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

    // If buffer is full, remove oldest message (drop-oldest policy)
    if (isFull())
    {
        LOG_W(TAG, "Buffer full, dropping oldest message");
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

    // Write to flash
    char filename[32];
    getMessageFilename(m_head, filename, sizeof(filename));

    File file = InternalFS.open(filename, FILE_O_WRITE);
    if (!file)
    {
        LOG_E(TAG, "Failed to open file for writing: %s", filename);
        return false;
    }

    size_t written = file.write(buffer, len);
    file.close();

    if (written != (size_t)len)
    {
        LOG_E(TAG, "Failed to write complete message to flash");
        return false;
    }

    // Update buffer state
    m_head = (m_head + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
    m_count++;
    saveState();

    LOG_I(TAG, "Message buffered to flash: %s", filename);

    return true;
}

bool MessageBuffer::peek(Message &msg)
{
    if (isEmpty() || !m_initialized)
    {
        return false;
    }

    // Read from flash
    char filename[32];
    getMessageFilename(m_tail, filename, sizeof(filename));

    // Check if file exists before trying to open
    if (!InternalFS.exists(filename))
    {
        LOG_W(TAG, "Message file missing (possible corruption): %s", filename);

        // File is missing but state says it should exist - corruption detected
        // Remove this entry from the queue and reset count
        LOG_I(TAG, "Auto-recovering: removing missing entry from queue");
        m_tail = (m_tail + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
        m_count--;
        if (m_count < 0)
            m_count = 0;
        saveState();

        return false;
    }

    File file = InternalFS.open(filename, FILE_O_READ);
    if (!file)
    {
        LOG_E(TAG, "Failed to open file for reading: %s", filename);
        return false;
    }

    // Read message data
    uint8_t buffer[BufferConstants::MAX_PROTOCOL_MESSAGE];
    size_t len = file.read(buffer, sizeof(buffer));
    file.close();

    if (len == 0)
    {
        LOG_W(TAG, "Empty message file - removing corrupt entry");
        // Remove corrupt empty file
        InternalFS.remove(filename);
        m_tail = (m_tail + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
        m_count--;
        if (m_count < 0)
            m_count = 0;
        saveState();
        return false;
    }

    // Deserialize
    if (!msg.deserialize(buffer, len))
    {
        LOG_W(TAG, "Failed to deserialize buffered message - removing corrupt entry");
        // Remove corrupt unreadable file
        InternalFS.remove(filename);
        m_tail = (m_tail + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
        m_count--;
        if (m_count < 0)
            m_count = 0;
        saveState();
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

    // Delete file
    char filename[32];
    getMessageFilename(m_tail, filename, sizeof(filename));

    if (InternalFS.exists(filename))
    {
        InternalFS.remove(filename);
        LOG_I(TAG, "Removed buffered message: %s", filename);
    }

    // Update buffer state
    m_tail = (m_tail + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
    m_count--;
    saveState();

    return true;
}

void MessageBuffer::clear()
{
    if (!m_initialized)
    {
        return;
    }

    LOG_I(TAG, "Clearing message buffer");

    // Delete all message files
    for (size_t i = 0; i < BufferConstants::MAX_BUFFERED_MESSAGES; i++)
    {
        char filename[32];
        getMessageFilename(i, filename, sizeof(filename));

        if (InternalFS.exists(filename))
        {
            InternalFS.remove(filename);
        }
    }

    // Reset state
    m_head = 0;
    m_tail = 0;
    m_count = 0;
    saveState();
}

void MessageBuffer::loadState()
{
    if (!InternalFS.exists(STATE_FILE))
    {
        LOG_I(TAG, "No saved buffer state, starting fresh");
        m_head = 0;
        m_tail = 0;
        m_count = 0;
        return;
    }

    File file = InternalFS.open(STATE_FILE, FILE_O_READ);
    if (!file)
    {
        LOG_E(TAG, "Failed to open state file");
        m_head = 0;
        m_tail = 0;
        m_count = 0;
        return;
    }

    // Read state: head, tail, count (3 x 4 bytes = 12 bytes)
    uint8_t state[12];
    size_t len = file.read(state, sizeof(state));
    file.close();

    if (len != sizeof(state))
    {
        LOG_W(TAG, "Invalid state file size");
        m_head = 0;
        m_tail = 0;
        m_count = 0;
        return;
    }

    // Deserialize state
    memcpy(&m_head, &state[0], 4);
    memcpy(&m_tail, &state[4], 4);
    memcpy(&m_count, &state[8], 4);

    // Validate state
    if (m_head < 0 || m_head >= (int)BufferConstants::MAX_BUFFERED_MESSAGES ||
        m_tail < 0 || m_tail >= (int)BufferConstants::MAX_BUFFERED_MESSAGES ||
        m_count < 0 || m_count > (int)BufferConstants::MAX_BUFFERED_MESSAGES)
    {
        LOG_W(TAG, "Corrupt buffer state (invalid indices), resetting");
        m_head = 0;
        m_tail = 0;
        m_count = 0;
        return;
    }

    // Verify that expected message files actually exist
    // If files are missing, the state is corrupt (incomplete write/filesystem issue)
    if (m_count > 0)
    {
        int missingFiles = 0;
        int idx = m_tail;
        for (int i = 0; i < m_count; i++)
        {
            char filename[32];
            getMessageFilename(idx, filename, sizeof(filename));

            if (!InternalFS.exists(filename))
            {
                LOG_W(TAG, "Expected message file missing: %s", filename);
                missingFiles++;
            }

            idx = (idx + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
        }

        if (missingFiles > 0)
        {
            LOG_W(TAG, "Found %d missing message files - clearing corrupt state", missingFiles);

            // Clear all message files and reset state
            clear();
            m_head = 0;
            m_tail = 0;
            m_count = 0;
            m_initialized = true; // Set before saveState
            saveState();
        }
        else
        {
            LOG_I(TAG, "All expected message files verified");
        }
    }
}

void MessageBuffer::saveState()
{
    if (!m_initialized)
    {
        return;
    }

    File file = InternalFS.open(STATE_FILE, FILE_O_WRITE);
    if (!file)
    {
        LOG_E(TAG, "Failed to save buffer state");
        return;
    }

    // Serialize state: head, tail, count (3 x 4 bytes = 12 bytes)
    uint8_t state[12];
    memcpy(&state[0], &m_head, 4);
    memcpy(&state[4], &m_tail, 4);
    memcpy(&state[8], &m_count, 4);

    file.write(state, sizeof(state));
    file.close();
}

void MessageBuffer::getMessageFilename(size_t index, char *pathBuf, size_t pathBufSize)
{
    snprintf(pathBuf, pathBufSize, "%s/msg%02d.bin", BUFFER_DIR, (int)index);
}
