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
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        LOG_W(TAG, "NVS partition needs erasing, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK)
    {
        LOG_E(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &m_nvsHandle);
    if (err != ESP_OK)
    {
        LOG_E(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    m_initialized = true;
    loadState();

    LOG_I(TAG, "MessageBuffer initialized: %d messages persisted", m_count);
    return true;
}

void MessageBuffer::loadState()
{
    uint32_t value;
    m_head = (nvs_get_u32(m_nvsHandle, NVS_KEY_HEAD, &value) == ESP_OK) ? value : 0;
    m_tail = (nvs_get_u32(m_nvsHandle, NVS_KEY_TAIL, &value) == ESP_OK) ? value : 0;
    m_count = (nvs_get_u32(m_nvsHandle, NVS_KEY_COUNT, &value) == ESP_OK) ? value : 0;
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

bool MessageBuffer::add(const meshtastic_FromRadio &msg)
{
    if (!m_initialized)
    {
        LOG_E(TAG, "MessageBuffer not initialized");
        return false;
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

    char key[16];
    getMessageKey(m_tail, key, sizeof(key));

    esp_err_t err = nvs_set_blob(m_nvsHandle, key, buffer, len);
    if (err != ESP_OK)
    {
        LOG_E(TAG, "Failed to write message to NVS: %s", esp_err_to_name(err));
        return false;
    }

    if (m_count >= (int)BufferConstants::MAX_BUFFERED_MESSAGES)
    {
        m_head = (m_head + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
        LOG_W(TAG, "Buffer full, dropping oldest message");
    }
    else
    {
        m_count++;
    }

    m_tail = (m_tail + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
    saveState();

    LOG_I(TAG, "Message buffered (%d bytes, total: %d)", len, m_count);
    return true;
}

bool MessageBuffer::peek(meshtastic_FromRadio &msg)
{
    if (!m_initialized || isEmpty()) return false;

    char key[16];
    getMessageKey(m_head, key, sizeof(key));

    uint8_t buffer[MeshtasticBLE::MAX_TO_FROM_RADIO_SIZE];
    size_t len = sizeof(buffer);

    esp_err_t err = nvs_get_blob(m_nvsHandle, key, buffer, &len);
    if (err != ESP_OK)
    {
        LOG_W(TAG, "Failed to read message from NVS, skipping");
        popFront();
        return !isEmpty() ? peek(msg) : false;
    }

    // Deserialize protobuf
    msg = meshtastic_FromRadio_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(buffer, len);
    if (!pb_decode(&stream, meshtastic_FromRadio_fields, &msg))
    {
        LOG_W(TAG, "Failed to deserialize FromRadio, skipping");
        popFront();
        return !isEmpty() ? peek(msg) : false;
    }

    return false;
}

bool MessageBuffer::popFront()
{
    if (!m_initialized || isEmpty()) return false;

    char key[16];
    getMessageKey(m_head, key, sizeof(key));

    esp_err_t err = nvs_erase_key(m_nvsHandle, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        LOG_W(TAG, "Failed to erase message from NVS: %s", esp_err_to_name(err));
    }

    m_head = (m_head + 1) % BufferConstants::MAX_BUFFERED_MESSAGES;
    m_count--;
    saveState();

    LOG_I(TAG, "Message removed from buffer (count=%d)", m_count);
    return true;
}

void MessageBuffer::clear()
{
    if (!m_initialized) return;

    for (size_t i = 0; i < BufferConstants::MAX_BUFFERED_MESSAGES; i++)
    {
        char key[16];
        getMessageKey(i, key, sizeof(key));
        nvs_erase_key(m_nvsHandle, key);
    }

    m_head = 0;
    m_tail = 0;
    m_count = 0;
    saveState();

    LOG_I(TAG, "MessageBuffer cleared");
}
