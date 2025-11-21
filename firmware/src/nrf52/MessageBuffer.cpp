#include "nrf52/MessageBuffer.h"
#include <Arduino.h>

MessageBuffer::MessageBuffer()
    : m_head(0), m_tail(0), m_count(0), m_initialized(false)
{
}

MessageBuffer::~MessageBuffer()
{
}

bool MessageBuffer::begin()
{
    Serial.println("Initializing MessageBuffer with LittleFS");

    // Initialize Internal File System
    InternalFS.begin();

    // Create buffer directory if it doesn't exist
    if (!InternalFS.exists(BUFFER_DIR))
    {
        Serial.println("Creating message buffer directory");
        InternalFS.mkdir(BUFFER_DIR);
    }

    // Load state from flash
    loadState();

    m_initialized = true;
    Serial.print("MessageBuffer initialized: ");
    Serial.print(m_count);
    Serial.println(" messages in buffer");

    return true;
}

bool MessageBuffer::add(const Message &msg)
{
    if (!m_initialized)
    {
        Serial.println("MessageBuffer not initialized");
        return false;
    }

    // If buffer is full, remove oldest message (drop-oldest policy)
    if (isFull())
    {
        Serial.println("Buffer full, dropping oldest message");
        popFront();
    }

    // Serialize message
    uint8_t buffer[MAX_MESSAGE_SIZE];
    int len = msg.serialize(buffer, sizeof(buffer));

    if (len <= 0)
    {
        Serial.println("Failed to serialize message for buffering");
        return false;
    }

    // Write to flash
    char filename[32];
    getMessageFilename(m_head, filename, sizeof(filename));

    File file = InternalFS.open(filename, FILE_O_WRITE);
    if (!file)
    {
        Serial.print("Failed to open file for writing: ");
        Serial.println(filename);
        return false;
    }

    size_t written = file.write(buffer, len);
    file.close();

    if (written != (size_t)len)
    {
        Serial.println("Failed to write complete message to flash");
        return false;
    }

    // Update buffer state
    m_head = (m_head + 1) % MAX_MESSAGES;
    m_count++;
    saveState();

    Serial.print("Message buffered to flash: ");
    Serial.println(filename);

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

    File file = InternalFS.open(filename, FILE_O_READ);
    if (!file)
    {
        Serial.print("Failed to open file for reading: ");
        Serial.println(filename);
        return false;
    }

    // Read message data
    uint8_t buffer[MAX_MESSAGE_SIZE];
    size_t len = file.read(buffer, sizeof(buffer));
    file.close();

    if (len == 0)
    {
        Serial.println("Empty message file");
        return false;
    }

    // Deserialize
    if (!msg.deserialize(buffer, len))
    {
        Serial.println("Failed to deserialize buffered message");
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
        Serial.print("Removed buffered message: ");
        Serial.println(filename);
    }

    // Update buffer state
    m_tail = (m_tail + 1) % MAX_MESSAGES;
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

    Serial.println("Clearing message buffer");

    // Delete all message files
    for (size_t i = 0; i < MAX_MESSAGES; i++)
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
        Serial.println("No saved buffer state, starting fresh");
        m_head = 0;
        m_tail = 0;
        m_count = 0;
        return;
    }

    File file = InternalFS.open(STATE_FILE, FILE_O_READ);
    if (!file)
    {
        Serial.println("Failed to open state file");
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
        Serial.println("Invalid state file size");
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
    if (m_head < 0 || m_head >= (int)MAX_MESSAGES ||
        m_tail < 0 || m_tail >= (int)MAX_MESSAGES ||
        m_count < 0 || m_count > (int)MAX_MESSAGES)
    {
        Serial.println("Corrupt buffer state, resetting");
        m_head = 0;
        m_tail = 0;
        m_count = 0;
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
        Serial.println("Failed to save buffer state");
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
