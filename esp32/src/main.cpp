//! ESP32 Firmware for LoRa-BLE Bridge (Power-Optimized)
//!
//! This firmware implements a BLE peripheral that communicates with Android devices
//! and bridges BLE messages to LoRa transmission and reception.
//!
//! Features:
//! - BLE GATT server with TX/RX characteristics for message exchange
//! - LoRa radio for long-range communication (5-10 km typical)
//! - Message queue for inter-task communication
//! - Message buffering (up to 10 messages) when BLE disconnected
//! - Light sleep for power optimization
//! - Interrupt-driven LoRa reception (always listening)
#include <Arduino.h>
#include <RadioLib.h>
#include "BLEManager.h"
#include "Protocol.h"
#include "LEDManager.h"
#include "MessageBuffer.h"
#include <freertos/queue.h>
#include <esp_task_wdt.h>
#include <freertos/task.h>
#include <esp_wifi.h>
#include "esp_pm.h"
#include <esp_sleep.h>

// RadioLib SX1278 radio instance
SX1278 radio = new Module(LORA_SS, LORA_DIO0, LORA_RST, RADIOLIB_NC);
#ifdef LED_PIN
LEDManager ledManager(LED_PIN);
#endif

// Message queues using FreeRTOS
const int BLE_TO_LORA_QUEUE_SIZE = 10;
const int LORA_TO_BLE_QUEUE_SIZE = 15;

QueueHandle_t bleToLoraQueue;
QueueHandle_t loraToBleQueue;

// Struct for LoRa packets with metadata
struct LoRaPacket
{
    uint8_t buffer[256];
    int len;
    int rssi;
    float snr;
};

QueueHandle_t loRaQueue;

// BLEManager declared after queues
BLEManager *bleManager;

// Message buffer for when BLE is disconnected (SINGLE GLOBAL INSTANCE)
MessageBuffer messageBuffer;

// Flag for LoRa activity (set in ISR, checked in loop)
volatile bool loraPacketReceived = false;

// Power management state
enum PowerState
{
    STATE_DISCONNECTED_ADVERTISING,
    STATE_DISCONNECTED_SLEEPING,
    STATE_CONNECTED
};

PowerState powerState = STATE_DISCONNECTED_ADVERTISING;
unsigned long advertiseStartMillis = 0;
unsigned long lastActivityMillis = 0;
static const unsigned long ADVERTISE_MS = 30000UL; // 30 seconds advertise window
static const unsigned long INACTIVE_MS = 60000UL;  // 60 seconds inactive window

/**
 * @brief LoRa receive callback - handles incoming LoRa packets event-driven (ISR)
 * IMPORTANT: ISR should ONLY set flag - all data reading happens in main loop
 * Following RadioLib best practices from examples
 */
#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void onLoRaReceive(void)
{
    // Set flag only - do NOT read data in ISR!
    loraPacketReceived = true;
}

void printWakeupReason()
{
    // Log wakeup reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    Serial.print("Power: Woke from light sleep - reason: ");
    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_GPIO:
        Serial.println("GPIO");
        break;
    case ESP_SLEEP_WAKEUP_EXT0:
        Serial.println("EXT0 (wake button)");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        Serial.println("Timer");
        break;
    default:
        Serial.print("Unknown (");
        Serial.print(wakeup_reason);
        Serial.println(")");
        break;
    }
}

/**
 * @brief Configure power management settings
 */
void configurePowerManagement()
{
    Serial.println("Disabling WiFi and Bluetooth Classic for power savings");

    printWakeupReason();

#if CONFIG_IDF_TARGET_ESP32
    esp_pm_config_esp32_t pm_config = {
        .max_freq_mhz = CPU_FREQ_MHZ,
        .min_freq_mhz = 10,
        .light_sleep_enable = true,
    };
#elif CONFIG_IDF_TARGET_ESP32S3
    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = CPU_FREQ_MHZ,
        .min_freq_mhz = 10,
        .light_sleep_enable = true,
    };
#endif

    esp_pm_configure(&pm_config);

    // Disable WiFi completely (saves ~50-80 mA)
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_INIT)
    {
        esp_wifi_deinit();
        Serial.println("WiFi disabled successfully");
    }
    else
    {
        Serial.printf("WiFi stop failed: %d (may not be initialized)\n", err);
    }

    // Disable Bluetooth Classic (we only use BLE via NimBLE)
    btStop();
    Serial.println("Bluetooth Classic disabled (using NimBLE for BLE only)");

    // Set initial CPU frequency to match power management max
    setCpuFrequencyMhz(CPU_FREQ_MHZ);
    Serial.print("CPU Frequency: ");
    Serial.print(getCpuFrequencyMhz());
    Serial.println(" MHz");

    Serial.println("Power management configured (light sleep enabled)");
}

/**
 * @brief Initialize FreeRTOS message queues
 */
void initializeMessageQueues()
{
    bleToLoraQueue = xQueueCreate(BLE_TO_LORA_QUEUE_SIZE, sizeof(Message));
    loraToBleQueue = xQueueCreate(LORA_TO_BLE_QUEUE_SIZE, sizeof(Message));
    loRaQueue = xQueueCreate(15, sizeof(LoRaPacket));

    if (bleToLoraQueue == nullptr || loraToBleQueue == nullptr || loRaQueue == nullptr)
    {
        Serial.println("Failed to create message queues. Halting execution.");
        while (1)
        {
            delay(1000);
        }
    }
}

/**
 * @brief Initialize BLE manager with retry logic
 */
void initializeBLE()
{
    bleManager = new BLEManager(bleToLoraQueue);

    const int BLE_RETRY_COUNT = 3;
    int bleRetries = BLE_RETRY_COUNT;
    bool bleSuccess = false;

    while (bleRetries > 0 && !bleSuccess)
    {
        Serial.print("BLE setup attempt ");
        Serial.print(BLE_RETRY_COUNT - bleRetries + 1);
        Serial.print("/");
        Serial.println(BLE_RETRY_COUNT);

        if (bleManager->setup(DEVICE_NAME))
        {
            bleSuccess = true;
            Serial.println("BLE setup successful");
        }
        else
        {
            Serial.println("BLE setup failed");
            if (bleRetries > 1)
            {
                Serial.println("Retrying in 2 seconds...");
                delay(2000);
            }
            bleRetries--;
        }
    }

    if (!bleSuccess)
    {
        Serial.println("BLE setup failed permanently. Halting execution.");
        while (1)
        {
            delay(1000);
        }
    }

    bleManager->startAdvertising();
}

/**
 * @brief Initialize LoRa radio with retry logic
 */
void initializeLoRa()
{
    Serial.println("Initializing LoRa radio (RadioLib)");
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);

    const int LORA_RETRY_COUNT = 3;
    int loraRetries = LORA_RETRY_COUNT;
    bool loraSuccess = false;

    while (loraRetries > 0 && !loraSuccess)
    {
        Serial.print("LoRa setup attempt ");
        Serial.print(LORA_RETRY_COUNT - loraRetries + 1);
        Serial.print("/");
        Serial.println(LORA_RETRY_COUNT);

        int state = radio.begin(LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                                LORA_CODING_RATE, 0x12, LORA_TX_POWER);

        if (state == RADIOLIB_ERR_NONE)
        {
            loraSuccess = true;
            Serial.println("LoRa setup successful");
            Serial.print("  Frequency: ");
            Serial.print(LORA_FREQUENCY);
            Serial.println(" MHz");
            Serial.print("  Bandwidth: ");
            Serial.print(LORA_BANDWIDTH);
            Serial.println(" kHz");
            Serial.print("  Spreading Factor: ");
            Serial.println(LORA_SPREADING_FACTOR);
            Serial.print("  Coding Rate: 4/");
            Serial.println(LORA_CODING_RATE);
            Serial.print("  TX Power: ");
            Serial.print(LORA_TX_POWER);
            Serial.println(" dBm");
        }
        else
        {
            Serial.print("LoRa setup failed, code ");
            Serial.println(state);
            if (loraRetries > 1)
            {
                Serial.println("Retrying in 1 second...");
                delay(1000);
            }
            loraRetries--;
        }
    }

    if (!loraSuccess)
    {
        Serial.println("LoRa setup failed permanently. Halting execution.");
        while (1)
        {
            delay(1000);
        }
    }

    // Set up event-driven LoRa reception
    radio.setPacketReceivedAction(onLoRaReceive);

    int state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.print("Failed to start receive mode, code ");
        Serial.println(state);
    }
}

/**
 * @brief Configure GPIO wake-up sources for light sleep
 */
void configureWakeupSources()
{
    // Configure wake button (ext0 for LOW trigger on RTC GPIO)
    // sp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_BUTTON, 0);
    gpio_wakeup_enable((gpio_num_t)WAKE_BUTTON, GPIO_INTR_LOW_LEVEL);

    // Configure LoRa DIO0 wake (gpio_wakeup for HIGH trigger)
    // esp_sleep_enable_ext1_wakeup(1ULL << LORA_DIO0, ESP_EXT1_WAKEUP_ANY_HIGH);
    gpio_wakeup_enable((gpio_num_t)LORA_DIO0, GPIO_INTR_HIGH_LEVEL);

    Serial.print("GPIO wake-up configured: Boot button (GPIO");
    Serial.print(WAKE_BUTTON);
    Serial.print(" on LOW), LoRa DIO0 (GPIO");
    Serial.print(LORA_DIO0);
    Serial.println(" on HIGH)");
}

/**
 * @brief Setup routine for ESP32 LoRa-BLE Bridge
 */
void setup()
{
    Serial.begin(115200);

    configurePowerManagement();

    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    Serial.println("ESP32 LoRa-BLE Bridge starting");

    initializeMessageQueues();
    initializeBLE();
    initializeLoRa();
    configureWakeupSources();

#ifdef LED_PIN
    ledManager.setup();
#endif

    Serial.println("All systems initialized");
}

/**
 * @brief Send all buffered messages when BLE reconnects
 */
void sendBufferedMessages()
{
    if (!bleManager->isConnected() || messageBuffer.isEmpty())
    {
        return;
    }

    Serial.print("BLE connected - sending ");
    Serial.print(messageBuffer.getCount());
    Serial.println(" buffered messages");

    Message bufferedMsg;
    while (messageBuffer.get(bufferedMsg))
    {
        if (bleManager->sendMessage(bufferedMsg))
        {
            Serial.println("Buffered message sent successfully");
#ifdef LED_PIN
            ledManager.blink();
#endif
            delay(20); // Small delay between messages to avoid overwhelming BLE
        }
        else
        {
            Serial.println("Failed to send buffered message - keeping remaining messages in buffer");
            messageBuffer.add(bufferedMsg);
            break;
        }
    }
}

/**
 * @brief Process queued messages from LoRa to BLE
 */
void processLoRaToBleQueue()
{
    Message loraMsg;
    if (xQueueReceive(loraToBleQueue, &loraMsg, 0) != pdTRUE)
    {
        return;
    }

    if (bleManager->isConnected())
    {
        if (bleManager->sendMessage(loraMsg))
        {
            Serial.println("Message forwarded from LoRa to BLE");
#ifdef LED_PIN
            ledManager.blink();
#endif
        }
    }
    else
    {
        messageBuffer.add(loraMsg);
        Serial.print("Buffered message (total: ");
        Serial.print(messageBuffer.getCount());
        Serial.println(")");
    }
}

/**
 * @brief Handle LoRa to BLE message forwarding and buffering
 */
void handleLoRaToBleForwarding()
{
    sendBufferedMessages();
    processLoRaToBleQueue();
}

/**
 * @brief Send ACK for received message
 */
void sendLoRaAck(uint32_t seq)
{
    Message ack = Message::createAck(seq);
    uint8_t ackBuf[64];
    int ackLen = ack.serialize(ackBuf, sizeof(ackBuf));

    if (ackLen <= 0)
    {
        return;
    }

    Serial.print("Sending ACK for seq: ");
    Serial.println(seq);

    // Clear RX interrupt handler to allow DIO0 to signal TX completion
    radio.clearPacketReceivedAction();

    // Reconfigure watchdog for 10 seconds
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());

    int state = radio.transmit(ackBuf, ackLen);

    // Restore normal watchdog timeout
    esp_task_wdt_init(5, true);
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());

    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println("ACK sent successfully");
    }
    else
    {
        Serial.print("ACK send failed, code ");
        Serial.println(state);
    }

    // Restore RX interrupt handler and return to RX mode
    radio.setPacketReceivedAction(onLoRaReceive);
    radio.startReceive();
}

/**
 * @brief Queue or buffer message for BLE delivery
 */
void queueMessageForBLE(const Message &msg)
{
    if (bleManager->isConnected())
    {
        if (xQueueSend(loraToBleQueue, &msg, 0) != pdTRUE)
        {
            Serial.println("Warning: LoRa to BLE queue full, buffering");
            messageBuffer.add(msg);
        }
    }
    else
    {
        messageBuffer.add(msg);
        Serial.print("Buffered message (total: ");
        Serial.print(messageBuffer.getCount());
        Serial.println(")");
    }
}

/**
 * @brief Handle received text message
 */
void handleTextMessage(const Message &msg)
{
    Serial.print("Text - seq: ");
    Serial.print(msg.textData.seq);
    Serial.print(", text: \"");
    Serial.print(msg.textData.text);
    Serial.print("\"");

    if (msg.textData.hasGps)
    {
        Serial.print(", GPS: ");
        Serial.print(msg.textData.lat / 1000000.0, 6);
        Serial.print("°, ");
        Serial.print(msg.textData.lon / 1000000.0, 6);
        Serial.print("°");
    }
    Serial.println();

    sendLoRaAck(msg.textData.seq);
    queueMessageForBLE(msg);

#ifdef LED_PIN
    ledManager.blink();
#endif
}

/**
 * @brief Handle received ACK message
 */
void handleAckMessage(const Message &msg)
{
    Serial.print("ACK - seq: ");
    Serial.println(msg.ackData.seq);

    queueMessageForBLE(msg);

#ifdef LED_PIN
    ledManager.blink();
#endif
}

/**
 * @brief Process received LoRa packet
 */
void processLoRaPacket(const LoRaPacket &packet)
{
    Serial.println("processLoRaPacket: packet received");
    lastActivityMillis = millis();

    if (!bleManager->isConnected())
    {
        Serial.println("processLoRaPacket: no BLE connection - buffering for later");
    }

    Serial.print("LoRa RX: ");
    Serial.print(packet.len);
    Serial.print(" bytes, RSSI: ");
    Serial.print(packet.rssi);
    Serial.print(" dBm, SNR: ");
    Serial.print(packet.snr);
    Serial.println(" dB");

    // Deserialize message
    Message msg;
    if (!msg.deserialize(packet.buffer, packet.len))
    {
        Serial.println("Failed to deserialize LoRa message");
        return;
    }

    Serial.print("Deserialized: type=");
    Serial.println((int)msg.type);

    // Handle message types
    switch (msg.type)
    {
    case MessageType::Text:
        handleTextMessage(msg);
        break;

    case MessageType::Ack:
        handleAckMessage(msg);
        break;
    }

    // Handle wake from sleep scenario
    if (!bleManager->isConnected())
    {
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        if (cause == ESP_SLEEP_WAKEUP_GPIO)
        {
            Serial.println("processLoRaPacket: woke from LoRa GPIO - message buffered, will start advertising");
        }
    }
}

/**
 * @brief Transmit message via LoRa radio
 */
void transmitLoRaMessage(const uint8_t *buffer, int length)
{
    Serial.print("Transmitting ");
    Serial.print(length);
    Serial.println(" bytes via LoRa");

    // Clear RX interrupt handler to allow DIO0 to signal TX completion
    radio.clearPacketReceivedAction();

    // Reconfigure watchdog for 10 seconds to allow long LoRa transmission
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());

    int state = radio.transmit(buffer, length);

    // Restore normal watchdog timeout (5 seconds)
    esp_task_wdt_init(5, true);
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());

    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println("LoRa TX successful");
#ifdef LED_PIN
        ledManager.blink(2);
#endif
    }
    else
    {
        Serial.print("LoRa TX failed, code ");
        Serial.println(state);
    }

    // Restore RX interrupt handler and return to RX mode
    radio.setPacketReceivedAction(onLoRaReceive);
    radio.startReceive();
    delay(50);
}

/**
 * @brief Process messages from BLE queue and transmit via LoRa
 */
void processBleToLoRaQueue()
{
    Message bleMsg;
    if (xQueueReceive(bleToLoraQueue, &bleMsg, 0) != pdTRUE)
    {
        return;
    }

    Serial.print("Received from BLE queue: type=");
    Serial.println((int)bleMsg.type);

    // Serialize and send via LoRa
    uint8_t buf[64];
    int len = bleMsg.serialize(buf, sizeof(buf));

    if (len > 0)
    {
        transmitLoRaMessage(buf, len);
    }
    else
    {
        Serial.println("Failed to serialize message for LoRa TX");
    }
}

/**
 * @brief Read and process LoRa packet if available
 */
void checkAndProcessLoRaPacket()
{
    if (!loraPacketReceived)
    {
        return;
    }

    loraPacketReceived = false;

    // Read packet data in main loop (NOT in ISR)
    LoRaPacket packet;
    int state = radio.readData(packet.buffer, sizeof(packet.buffer));

    if (state == RADIOLIB_ERR_NONE)
    {
        packet.len = radio.getPacketLength();
        packet.rssi = radio.getRSSI();
        packet.snr = radio.getSNR();
        processLoRaPacket(packet);
    }
    else if (state == RADIOLIB_ERR_CRC_MISMATCH)
    {
        Serial.println("LoRa RX: CRC error");
    }
    else
    {
        Serial.print("LoRa RX failed, code ");
        Serial.println(state);
    }

    // Restart receive mode
    radio.startReceive();
}

/**
 * @brief Handle BLE connected state and inactivity timeout
 */
void handleConnectedState()
{
    if (powerState != STATE_CONNECTED)
    {
        Serial.println("Power: Entering CONNECTED state (always active)");
        powerState = STATE_CONNECTED;
        bleManager->stopAdvertising();
    }

    if (lastActivityMillis == 0)
    {
        lastActivityMillis = millis();
    }

    // 60s inactivity timeout -> force disconnect
    if ((millis() - lastActivityMillis) > INACTIVE_MS)
    {
        Serial.println("Power: Inactivity timeout - disconnecting BLE client");
        bleManager->disconnect();
        powerState = STATE_DISCONNECTED_ADVERTISING;
        advertiseStartMillis = millis();
    }
}

/**
 * @brief Handle disconnected advertising state
 */
void handleAdvertisingState()
{
    // Ensure advertising is active
    if (advertiseStartMillis == 0)
    {
        Serial.println("Power: startAdvertising");
        bleManager->startAdvertising();
        advertiseStartMillis = millis();
    }

    if (millis() - advertiseStartMillis < ADVERTISE_MS)
    {
        return;
    }

    Serial.print("Power: Advertising period ended (timeout=");
    Serial.print(ADVERTISE_MS);
    Serial.println(" ms) - entering light sleep until button press or LoRa activity");

    // Re-enable GPIO wakeup before each sleep
    esp_sleep_enable_gpio_wakeup();

    Serial.flush();
    esp_light_sleep_start();

    printWakeupReason();

    // Restart advertising after wake
    Serial.println("Power: Restarting advertising after wake");
    advertiseStartMillis = 0;
    powerState = STATE_DISCONNECTED_ADVERTISING;
}

/**
 * @brief Manage power states and transitions
 */
void managePowerState()
{
    bool connected = (bleManager && bleManager->isConnected());

    if (connected)
    {
        handleConnectedState();
    }
    else
    {
        // Transition from connected to disconnected
        if (powerState == STATE_CONNECTED)
        {
            Serial.println("Power: BLE disconnected - switching to DISCONNECTED_ADVERTISING");
            powerState = STATE_DISCONNECTED_ADVERTISING;
            advertiseStartMillis = 0;
        }

        if (powerState == STATE_DISCONNECTED_ADVERTISING)
        {
            handleAdvertisingState();
        }
    }
}

/**
 * @brief Main loop - handles BLE<->LoRa message bridging with light sleep for power savings
 */
void loop()
{
    esp_task_wdt_reset();

    bleManager->process();
    processBleToLoRaQueue();
    checkAndProcessLoRaPacket();
    handleLoRaToBleForwarding();
    managePowerState();

    vTaskDelay(10);
}
