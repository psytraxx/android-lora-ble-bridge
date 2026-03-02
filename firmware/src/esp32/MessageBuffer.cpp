#include "esp32/MessageBuffer.h"
#include "common/Logging.h"
#include <cstring>
#include <Arduino.h>

static const char *TAG = "MsgBuf";

MessageBuffer::MessageBuffer()
    : m_nvsHandle(0),
      m_head(0),
      m_tail(0),
      m_count(0),
      m_initialized(false)
{
}

MessageBuffer::~MessageBuffer()
{
    if (m_initialized)
    {
        nvs_close(m_nvsHandle);
    }
}

bool MessageBuffer::begin()
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS partition was truncated and needs to be erased
        LOG_W(TAG, "NVS partition needs erasing, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK)
    {
        LOG_E(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return false;
    }

    // Open NVS namespace
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &m_nvsHandle);
    if (err != ESP_OK)
    {
        LOG_E(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    m_initialized = true;

    // Load persisted state
    loadState();

    LOG_I(TAG, "MessageBuffer initialized: %d messages persisted", m_count);
    return true;
}

void MessageBuffer::loadState()
{
    // Load head, tail, and count from NVS
    uint32_t value;

    if (nvs_get_u32(m_nvsHandle, NVS_KEY_HEAD, &value) == ESP_OK)
    {
        m_head = value;
    }
    else
    {
        m_head = 0;
    }

    if (nvs_get_u32(m_nvsHandle, NVS_KEY_TAIL, &value) == ESP_OK)
    {
        m_tail = value;
    }
    else
    {
        m_tail = 0;
    }

    if (nvs_get_u32(m_nvsHandle, NVS_KEY_COUNT, &value) == ESP_OK)
    {
        m_count = value;
    }
    else
    {
        m_count = 0;
    }

    LOG_I(TAG, "Loaded state: head=%d, tail=%d, count=%d", m_head, m_tail, m_count);
}

void MessageBuffer::saveState()
{
    nvs_set_u32(m_nvsHandle, NVS_KEY_HEAD, m_head);
    nvs_set_u32(m_nvsHandle, NVS_KEY_TAIL, m_tail);
    nvs_set_u32(m_nvsHandle, NVS_KEY_COUNT, m_count);
    nvs_commit(m_nvsHandle);
}

void MessageBuffer::getMessageKey(size_t index, char *keyBuf, size_t keyBufSize)
{
    snprintf(keyBuf, keyBufSize, "msg_%zu", index % BufferConstants::MAX_BUFFERED_MESSAGES);
}

bool MessageBuffer::add(const Message &msg)
{
    if (!m_initialized)
    {
        LOG_E(TAG, "MessageBuffer not initialized");
        return false;
    }

    // Serialize message
    uint8_t buffer[BufferConstants::MAX_PROTOCOL_MESSAGE];
    int len = msg.serialize(buffer, sizeof(buffer));

    if (len < 0)
    {
        LOG_E(TAG, "Failed to serialize message");
        return false;
    }

    // Generate key for this message slot
    char key[16];
    getMessageKey(m_tail, key, sizeof(key));

    // Store serialized message in NVS as blob
    esp_err_t err = nvs_set_blob(m_nvsHandle, key, buffer, len);
    if (err != ESP_OK)
    {
        LOG_E(TAG, "Failed to write message to NVS: %s", esp_err_to_name(err));
        return false;
    }

    // Update buffer state (drop-oldest if full)
    if (m_count >= (int)BufferConstants::MAX_BUFFERED_MESSAGES)
    {
        // Buffer full - overwrite oldest message
        m_head = (m_head + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
        LOG_W(TAG, "Buffer full, dropping oldest message (head moved to %d)", m_head);
    }
    else
    {
        m_count++;
    }

    m_tail = (m_tail + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;

    // Persist state
    saveState();

    LOG_I(TAG, "Message added to buffer (count=%d)", m_count);

    return true;
}

bool MessageBuffer::peek(Message &msg)
{
    if (!m_initialized)
    {
        LOG_E(TAG, "MessageBuffer not initialized");
        return false;
    }

    // Iterate (not recurse) over corrupted entries to avoid stack overflow
    // on constrained devices with many consecutive bad NVS entries.
    while (!isEmpty())
    {
        char key[16];
        getMessageKey(m_head, key, sizeof(key));

        uint8_t buffer[BufferConstants::MAX_PROTOCOL_MESSAGE];
        size_t len = sizeof(buffer);

        esp_err_t err = nvs_get_blob(m_nvsHandle, key, buffer, &len);
        if (err != ESP_OK)
        {
            LOG_W(TAG, "Failed to read message from NVS: %s (corrupted entry, skipping)", esp_err_to_name(err));
            popFront();
            LOG_I(TAG, "Skipped corrupted message (count=%d)", m_count);
            continue;
        }

        if (!msg.deserialize(buffer, len))
        {
            LOG_W(TAG, "Failed to deserialize message from NVS (corrupted data, skipping)");
            popFront();
            LOG_I(TAG, "Skipped corrupted message (count=%d)", m_count);
            continue;
        }

        return true;
    }

    return false;
}

bool MessageBuffer::popFront()
{
    if (!m_initialized)
    {
        LOG_E(TAG, "MessageBuffer not initialized");
        return false;
    }

    if (isEmpty())
    {
        return false;
    }

    // Generate key for message at head
    char key[16];
    getMessageKey(m_head, key, sizeof(key));

    // Erase message from NVS
    esp_err_t err = nvs_erase_key(m_nvsHandle, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        LOG_W(TAG, "Failed to erase message from NVS: %s", esp_err_to_name(err));
        // Continue anyway to update state
    }

    // Update buffer state
    m_head = (m_head + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
    m_count--;

    // Persist state
    saveState();

    LOG_I(TAG, "Message removed from buffer (count=%d)", m_count);

    return true;
}

void MessageBuffer::clear()
{
    if (!m_initialized)
    {
        LOG_E(TAG, "MessageBuffer not initialized");
        return;
    }

    // Erase all message keys
    for (size_t i = 0; i < BufferConstants::MAX_BUFFERED_MESSAGES; i++)
    {
        char key[16];
        getMessageKey(i, key, sizeof(key));
        nvs_erase_key(m_nvsHandle, key);
    }

    // Reset state
    m_head = 0;
    m_tail = 0;
    m_count = 0;

    saveState();

    LOG_I(TAG, "MessageBuffer cleared");
}
