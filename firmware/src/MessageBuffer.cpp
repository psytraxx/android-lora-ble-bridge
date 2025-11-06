#include "MessageBuffer.h"
#include <cstring>
#include <Arduino.h>
#include <Preferences.h>

MessageBuffer::MessageBuffer()
    : m_head(0),
      m_tail(0),
      m_count(0),
      m_initialized(false)
{
}

MessageBuffer::~MessageBuffer()
{
    if (m_initialized)
    {
        m_preferences.end();
    }
}

bool MessageBuffer::begin()
{
    // Open Preferences namespace
    if (!m_preferences.begin(NVS_NAMESPACE, false))
    {
        Serial.printf("Failed to open Preferences namespace\n");
        return false;
    }

    m_initialized = true;

    // Load persisted state
    loadState();

    Serial.printf("MessageBuffer initialized: %d messages persisted\n", m_count);

    return true;
}

void MessageBuffer::loadState()
{
    // Load head, tail, and count from Preferences
    m_head = m_preferences.getUInt(NVS_KEY_HEAD, 0);
    m_tail = m_preferences.getUInt(NVS_KEY_TAIL, 0);
    m_count = m_preferences.getInt(NVS_KEY_COUNT, 0);

    Serial.printf("Loaded state: head=%d, tail=%d, count=%d\n", m_head, m_tail, m_count);
}

void MessageBuffer::saveState()
{
    m_preferences.putUInt(NVS_KEY_HEAD, m_head);
    m_preferences.putUInt(NVS_KEY_TAIL, m_tail);
    m_preferences.putInt(NVS_KEY_COUNT, m_count);
}

void MessageBuffer::getMessageKey(size_t index, char *keyBuf, size_t keyBufSize)
{
    snprintf(keyBuf, keyBufSize, "msg_%zu", index % MAX_MESSAGES);
}

bool MessageBuffer::add(const Message &msg)
{
    if (!m_initialized)
    {
        Serial.printf("MessageBuffer not initialized\n");
        return false;
    }

    // Serialize message
    uint8_t buffer[MAX_MESSAGE_SIZE];
    int len = msg.serialize(buffer, sizeof(buffer));

    if (len < 0)
    {
        Serial.printf("Failed to serialize message\n");
        return false;
    }

    // Generate key for this message slot
    char key[16];
    getMessageKey(m_tail, key, sizeof(key));

    // Store serialized message in Preferences as blob
    size_t written = m_preferences.putBytes(key, buffer, len);
    if (written != len)
    {
        Serial.printf("Failed to write message to Preferences\n");
        return false;
    }

    // Update buffer state (drop-oldest if full)
    if (m_count >= (int)MAX_MESSAGES)
    {
        // Buffer full - overwrite oldest message
        m_head = (m_head + 1) % MAX_MESSAGES;
        Serial.printf("Buffer full, dropping oldest message (head moved to %d)\n", m_head);
    }
    else
    {
        m_count++;
    }

    m_tail = (m_tail + 1) % MAX_MESSAGES;

    // Persist state
    saveState();

    Serial.printf("Message added to buffer (count=%d)\n", m_count);

    return true;
}

bool MessageBuffer::peek(Message &msg)
{
    if (!m_initialized)
    {
        Serial.printf("MessageBuffer not initialized\n");
        return false;
    }

    if (isEmpty())
    {
        return false;
    }

    // Generate key for message at head (oldest)
    char key[16];
    getMessageKey(m_head, key, sizeof(key));

    // Read serialized message from Preferences
    uint8_t buffer[MAX_MESSAGE_SIZE];
    size_t len = m_preferences.getBytesLength(key);

    if (len == 0 || len > MAX_MESSAGE_SIZE)
    {
        Serial.printf("Invalid message length in Preferences: %zu\n", len);
        return false;
    }

    size_t read = m_preferences.getBytes(key, buffer, len);
    if (read != len)
    {
        Serial.printf("Failed to read message from Preferences\n");
        return false;
    }

    // Deserialize message
    if (!msg.deserialize(buffer, len))
    {
        Serial.printf("Failed to deserialize message from Preferences\n");
        return false;
    }

    return true;
}

bool MessageBuffer::popFront()
{
    if (!m_initialized)
    {
        Serial.printf("MessageBuffer not initialized\n");
        return false;
    }

    if (isEmpty())
    {
        return false;
    }

    // Generate key for message at head
    char key[16];
    getMessageKey(m_head, key, sizeof(key));

    // Remove message from Preferences
    bool erased = m_preferences.remove(key);
    if (!erased)
    {
        Serial.printf("Warning: Failed to erase message from Preferences\n");
        // Continue anyway to update state
    }

    // Update buffer state
    m_head = (m_head + 1) % MAX_MESSAGES;
    m_count--;

    // Persist state
    saveState();

    Serial.printf("Message removed from buffer (count=%d)\n", m_count);

    return true;
}

void MessageBuffer::clear()
{
    if (!m_initialized)
    {
        Serial.printf("MessageBuffer not initialized\n");
        return;
    }

    // Erase all message keys
    for (size_t i = 0; i < MAX_MESSAGES; i++)
    {
        char key[16];
        getMessageKey(i, key, sizeof(key));
        m_preferences.remove(key);
    }

    // Reset state
    m_head = 0;
    m_tail = 0;
    m_count = 0;

    saveState();

    Serial.printf("MessageBuffer cleared\n");
}
