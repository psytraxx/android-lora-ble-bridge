//! Unified Firmware for LoRa-BLE Bridge (Meshtastic Native)
//!
//! This single main.cpp works on both ESP32 and nRF52 by using platform traits.
//! Platform-specific behavior is selected at compile-time via PlatformTraits.
//!
//! Architecture:
//! - Meshtastic BLE service (FromRadio/ToRadio/FromNum)
//! - Native protobuf message queues (no custom protocol)
//! - Meshtastic OTA packet format (header + AES-encrypted payload)
//! - Priority TX queue with CSMA/CA, retry, implicit ACK, dupe cancellation

#include <Arduino.h>
#include <memory>
#include "common/Logging.h"
#include "common/LoRaManager.h"
#include "common/MessageQueue.h"
#include "common/FirmwareConfig.h"
#include "common/LEDManager.h"
#include "common/MeshProtocol.h"
#include "common/MeshPacket.h"
#include "common/NodeDB.h"
#include "common/PeerNodeDB.h"
#include "common/MeshCrypto.h"
#include "common/AppHandlers.h"
#include "common/ConfigManager.h"
#include "meshtastic/mesh.pb.h"
#include <pb_encode.h>
#include <pb_decode.h>

// Platform-specific FreeRTOS includes
#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#elif defined(ARDUINO_ARCH_NRF52)
#include <FreeRTOS.h>
#include <timers.h>
#endif

static const char *TAG = "Main";

// Select platform traits based on build target
#if defined(ARDUINO_ARCH_ESP32)
#include "esp32/PlatformTraits.h"
using Platform = ESP32PlatformTraits;
#define PLATFORM_NAME "ESP32"
#elif defined(ARDUINO_ARCH_NRF52)
#include "nrf52/PlatformTraits.h"
using Platform = NRF52PlatformTraits;
#define PLATFORM_NAME "nRF52"
#else
#error "Unsupported platform"
#endif

// ============================================================================
// Global Managers
// ============================================================================

static typename Platform::BLEManager *bleManager = nullptr;
static typename Platform::StorageManager *storageManager = nullptr;
static typename Platform::StorageManager storageManagerInstance;

// Meshtastic protobuf queues
MessageQueue<meshtastic_ToRadio> bleToLoraQueue;     // Accessed by AppHandlers
MessageQueue<meshtastic_FromRadio> loraToBleQueue;   // Non-static for AppHandlers admin responses

static LoRaManager *loraManager = nullptr;

#ifdef LED_PIN
static std::unique_ptr<LEDManager> ledManager(new LEDManager(LED_PIN, LEDConstants::HEARTBEAT_INTERVAL_MS, LEDConstants::HEARTBEAT_DURATION_MS));
#endif

static TimerHandle_t deepSleepTimerHandle = nullptr;

// Deadline after which the BLE GATT stack is considered ready for use.
// Avoids blocking the main loop after connection (replaces delay(500) in callback).
static uint32_t bleGattReadyAt = 0;

// Last received LoRa signal quality
static int lastRssi = 0;
static float lastSnr = 0.0f;

// Channel key buffer (populated per-packet from MeshProtocol multi-channel)
// No longer a single global — each RX/TX looks up the key by channel index

// FromRadio ID counter (non-static for AppHandlers admin responses)
uint32_t fromRadioId = 1;

// ============================================================================
// Implicit ACK Tracking (Phase D)
// ============================================================================

namespace
{
    constexpr size_t PENDING_ACK_SIZE = 8;
    constexpr uint32_t PENDING_ACK_TIMEOUT_MS = 30000; // 30 seconds

    struct PendingAckEntry
    {
        uint32_t fromNode;
        uint32_t packetId;
        uint32_t timestamp;
        bool active;
    };

    PendingAckEntry pendingAcks[PENDING_ACK_SIZE] = {};

    void recordPendingAck(uint32_t fromNode, uint32_t packetId)
    {
        uint32_t now = millis();

        // Find free or oldest expired slot
        size_t bestIdx = 0;
        uint32_t oldestTime = UINT32_MAX;

        for (size_t i = 0; i < PENDING_ACK_SIZE; i++)
        {
            if (!pendingAcks[i].active ||
                (now - pendingAcks[i].timestamp) >= PENDING_ACK_TIMEOUT_MS)
            {
                bestIdx = i;
                break;
            }
            if (pendingAcks[i].timestamp < oldestTime)
            {
                oldestTime = pendingAcks[i].timestamp;
                bestIdx = i;
            }
        }

        pendingAcks[bestIdx] = {fromNode, packetId, now, true};
        LOG_D(TAG, "Pending ACK recorded: from=%08lx id=%lu",
              (unsigned long)fromNode, (unsigned long)packetId);
    }

    bool checkImplicitAck(uint32_t fromNode, uint32_t packetId)
    {
        uint32_t now = millis();
        for (size_t i = 0; i < PENDING_ACK_SIZE; i++)
        {
            if (pendingAcks[i].active &&
                pendingAcks[i].fromNode == fromNode &&
                pendingAcks[i].packetId == packetId &&
                (now - pendingAcks[i].timestamp) < PENDING_ACK_TIMEOUT_MS)
            {
                pendingAcks[i].active = false;
                return true;
            }
        }
        return false;
    }
}

// ============================================================================
// Duplicate Detection Cache (Phase 3)
// ============================================================================

namespace
{
    constexpr size_t DEDUP_CACHE_SIZE = 32;
    constexpr uint32_t DEDUP_WINDOW_MS = 30000; // 30 seconds

    struct DedupEntry
    {
        uint32_t packetId;
        uint32_t nodeNum;
        uint32_t timestamp;
    };

    struct
    {
        DedupEntry entries[DEDUP_CACHE_SIZE];
        size_t nextIdx = 0;
    } dedupCache;

    bool isDuplicate(uint32_t fromNode, uint32_t packetId)
    {
        uint32_t now = millis();

        // Check existing entries
        for (size_t i = 0; i < DEDUP_CACHE_SIZE; i++)
        {
            const auto &entry = dedupCache.entries[i];
            if (entry.packetId == packetId &&
                entry.nodeNum == fromNode &&
                (now - entry.timestamp) < DEDUP_WINDOW_MS)
            {
                LOG_D(TAG, "Duplicate packet: from=%08lx, id=%lu",
                      (unsigned long)fromNode, (unsigned long)packetId);
                return true;
            }
        }

        // Add new entry (circular buffer)
        dedupCache.entries[dedupCache.nextIdx] = {packetId, fromNode, now};
        dedupCache.nextIdx = (dedupCache.nextIdx + 1) % DEDUP_CACHE_SIZE;
        return false;
    }

    // ========================================================================
    // Mesh Relay Logic (Phase 4 + Phase F: SNR-weighted delay)
    // ========================================================================

    constexpr uint32_t RELAY_MIN_INTERVAL_MS = 1000;  // Min 1s between relays
    constexpr uint8_t MAX_RELAYS_PER_MINUTE = 20;     // Duty cycle guard

    struct RelayState
    {
        uint32_t lastRelayTime = 0;
        uint32_t relayMinuteStart = 0;
        uint8_t relayCountThisMinute = 0;
    } relayState;

    bool canRelay()
    {
        uint32_t now = millis();

        // Reset minute counter if needed
        if ((now - relayState.relayMinuteStart) >= 60000)
        {
            relayState.relayMinuteStart = now;
            relayState.relayCountThisMinute = 0;
        }

        // Check rate limits
        if ((now - relayState.lastRelayTime) < RELAY_MIN_INTERVAL_MS)
        {
            LOG_D(TAG, "Relay suppressed: too soon (min interval)");
            return false;
        }

        if (relayState.relayCountThisMinute >= MAX_RELAYS_PER_MINUTE)
        {
            LOG_D(TAG, "Relay suppressed: exceeded %u/min limit", MAX_RELAYS_PER_MINUTE);
            return false;
        }

        return true;
    }

    // Phase F: SNR-weighted relay delay
    // Weaker SNR = shorter delay (relay first for better coverage)
    // Strong SNR = longer delay (let weaker nodes relay first)
    uint32_t calculateRelayDelay(float snr)
    {
        // Normalize SNR: -20dB -> 0.0, +10dB -> 1.0
        float normalized = constrain((snr + 20.0f) / 30.0f, 0.0f, 1.0f);

        // Weak signal (-20dB): ~200ms, Strong signal (+10dB): ~500ms
        uint32_t delayMs = 200 + (uint32_t)(normalized * 300.0f);

        // Add small random jitter to break ties
        #if defined(ARDUINO_ARCH_ESP32)
        delayMs += esp_random() % 50;
        #else
        delayMs += random(50);
        #endif

        return delayMs;
    }

    void scheduleRelay(const uint8_t *rawPacket, size_t len,
                       uint32_t fromNode, uint32_t packetId, float snr)
    {
        if (!canRelay())
        {
            return;
        }

        uint32_t delayMs = calculateRelayDelay(snr);

        if (loraManager->queueTransmit(rawPacket, len, TX_PRIORITY_RELAY,
                                       fromNode, packetId,
                                       true, // isRelay
                                       RetryConstants::MAX_RETRIES_RELAY,
                                       delayMs))
        {
            relayState.lastRelayTime = millis();
            relayState.relayCountThisMinute++;
            LOG_D(TAG, "Relay queued (delay=%lums, snr=%.1f)",
                  (unsigned long)delayMs, snr);
        }
        else
        {
            LOG_W(TAG, "Failed to queue relay");
        }
    }
}

// ============================================================================
// ACK Response Scheduling (Phase 3)
// ============================================================================

struct AckTimerData
{
    uint32_t toNode;
    uint32_t ackId;
    meshtastic_Routing_Error error;
};

void scheduleAckResponse(uint32_t toNode, uint32_t ackId,
                         meshtastic_Routing_Error error = meshtastic_Routing_Error_NONE)
{
    // Allocate timer data (freed in callback)
    AckTimerData *data = new AckTimerData{toNode, ackId, error};

    // Random delay 50-150ms (let sender switch to RX)
    #if defined(ARDUINO_ARCH_ESP32)
    uint32_t delayMs = 50 + (esp_random() % 100);
    #else
    uint32_t delayMs = 50 + (random(100));
    #endif

    TimerHandle_t ackTimer = xTimerCreate(
        "AckResponse",
        pdMS_TO_TICKS(delayMs),
        pdFALSE, // One-shot
        data,
        [](TimerHandle_t timer)
        {
            AckTimerData *data = (AckTimerData *)pvTimerGetTimerID(timer);
            AppHandlers::sendAck(data->toNode, data->ackId, data->error);
            delete data;
            xTimerDelete(timer, 0);
        });

    if (ackTimer != nullptr)
    {
        xTimerStart(ackTimer, 0);
        LOG_D(TAG, "ACK scheduled for node %08lx (delay=%lums)",
              (unsigned long)toNode, (unsigned long)delayMs);
    }
    else
    {
        LOG_W(TAG, "Failed to create ACK timer");
        delete data;
    }
}

// ============================================================================
// Forward Declarations
// ============================================================================

void onBleConnected();
void onBleDisconnected();
void onLoRaReceived(const LoRaPacket &packet);
void onLoRaTransmitted(bool success);
void handleToRadioPacket(const meshtastic_ToRadio &toRadio);

// ============================================================================
// Helper Functions
// ============================================================================

static inline void resetInactivityTimer()
{
    if (deepSleepTimerHandle != nullptr)
    {
        xTimerReset(deepSleepTimerHandle, 0);
    }
}

static void deepSleepTimerCallback(TimerHandle_t xTimer)
{
    (void)xTimer;

    LOG_I(TAG, "Inactivity timeout - entering deep sleep...");

    if (loraManager != nullptr)
    {
        if (!loraManager->startReceive(true))
        {
            LOG_E(TAG, "Failed to start LoRa continuous receive mode!");
        }
    }

    if (bleManager != nullptr)
    {
        bleManager->stopAdvertising();
    }

    Platform::enterDeepSleep();
    LOG_E(TAG, "Failed to enter deep sleep mode!");
}

// ============================================================================
// Helper: Determine TX priority from portnum
// ============================================================================

static TxPriority priorityFromPortnum(meshtastic_PortNum portnum)
{
    switch (portnum)
    {
    case meshtastic_PortNum_ROUTING_APP:
        return TX_PRIORITY_ACK;
    case meshtastic_PortNum_NODEINFO_APP:
    case meshtastic_PortNum_TELEMETRY_APP:
        return TX_PRIORITY_BROADCAST;
    default:
        return TX_PRIORITY_USER_TEXT;
    }
}

static uint8_t maxRetriesForPriority(TxPriority priority, bool wantAck)
{
    if (!wantAck)
        return 0;

    switch (priority)
    {
    case TX_PRIORITY_ACK:
        return RetryConstants::MAX_RETRIES_ACK;
    case TX_PRIORITY_USER_TEXT:
        return RetryConstants::MAX_RETRIES_USER;
    case TX_PRIORITY_RELAY:
        return RetryConstants::MAX_RETRIES_RELAY;
    case TX_PRIORITY_BROADCAST:
        return RetryConstants::MAX_RETRIES_BROADCAST;
    default:
        return 0;
    }
}

// ============================================================================
// Setup
// ============================================================================

void setup()
{
    Serial.begin(115200);

    LOG_I(TAG, "\n\n=== LoRa-BLE Bridge (Meshtastic) ===");
    LOG_I(TAG, "Platform: %s", PLATFORM_NAME);

    Platform::initializeWatchdog();
    Platform::initializePower();

#ifdef LED_PIN
    ledManager->setup();
#endif

    storageManager = &storageManagerInstance;
    if (!storageManager->begin())
    {
        LOG_I(TAG, "Storage initialization failed!");
    }

    // Initialize Meshtastic protocol (NodeDB, channel keys, ConfigManager)
    if (!MeshProtocol::init())
    {
        LOG_E(TAG, "Meshtastic protocol initialization failed!");
        while (1)
            ;
    }
    LOG_I(TAG, "Protocol: Meshtastic (sync word 0x2B)");

    // Construct Meshtastic device name: "Meshtastic_XXXX"
    char deviceName[20];
    uint32_t nodeNum = NodeDB::getOwnNodeNum();
    snprintf(deviceName, sizeof(deviceName), "Meshtastic_%04X",
             (unsigned int)(nodeNum & 0xFFFF));
    LOG_I(TAG, "Device: %s", deviceName);

    // Initialize BLE manager with ToRadio queue
    bleManager = new typename Platform::BLEManager(&bleToLoraQueue);

    if (!bleManager->setup(deviceName))
    {
        LOG_I(TAG, "BLE initialization failed!");
        while (1)
            ;
    }
    bleManager->setConnectionCallbacks(onBleConnected, onBleDisconnected);
    bleManager->startAdvertising();

    // Initialize LoRa
    loraManager = new LoRaManager(
        LORA_SCK, LORA_MISO, LORA_MOSI,
        LORA_SS, LORA_RST, LORA_DIO0, LORA_BUSY);

    loraManager->setReceiveCallback(onLoRaReceived);
    loraManager->setTransmitCallback(onLoRaTransmitted);

    bool initialized = false;
    if (Platform::isLoraWakeUp())
    {
        LOG_I(TAG, "Wakeup from LoRa detected, attempting to resume...");
        if (loraManager->handleSleepWakeup())
        {
            initialized = true;
            LOG_I(TAG, "LoRa resume successful");
        }
        else
        {
            LOG_W(TAG, "LoRa resume failed, falling back to full init");
        }
    }

    if (!initialized)
    {
        if (!loraManager->begin())
        {
            LOG_I(TAG, "LoRa initialization failed!");
            while (1)
                ;
        }
    }

    if (!loraManager->isTransmitting())
    {
        if (!loraManager->startReceive(true))
        {
            LOG_I(TAG, "Failed to start LoRa receive mode!");
        }
    }

    // Apply any persisted LoRa config (overrides compile-time LoRaConstants)
    ConfigManager::applyLoRaConfig(loraManager);

    // Deep sleep inactivity timer
    deepSleepTimerHandle = xTimerCreate(
        "DeepSleepTimer",
        pdMS_TO_TICKS(PowerConstants::INACTIVITY_TIMEOUT_MS),
        pdFALSE, (void *)0, deepSleepTimerCallback);

    if (deepSleepTimerHandle != nullptr)
    {
        if (xTimerStart(deepSleepTimerHandle, 0) == pdPASS)
        {
            LOG_I(TAG, "Deep sleep timer started (timeout: %lu ms)", PowerConstants::INACTIVITY_TIMEOUT_MS);
        }
    }

    // NodeInfo broadcast timer (30 seconds)
    TimerHandle_t nodeInfoTimer = xTimerCreate(
        "NodeInfo",
        pdMS_TO_TICKS(30000), // 30 seconds
        pdTRUE,               // Auto-reload
        nullptr,
        [](TimerHandle_t)
        { AppHandlers::broadcastNodeInfo(); });
    if (nodeInfoTimer != nullptr)
    {
        xTimerStart(nodeInfoTimer, 0);
        LOG_I(TAG, "NodeInfo broadcast timer started (30s interval)");
    }

    // Telemetry broadcast timer (60 seconds, delayed start)
    TimerHandle_t telemetryTimer = xTimerCreate(
        "Telemetry",
        pdMS_TO_TICKS(60000), // 60 seconds
        pdTRUE,               // Auto-reload
        nullptr,
        [](TimerHandle_t)
        { AppHandlers::broadcastTelemetry(); });
    if (telemetryTimer != nullptr)
    {
        TimerHandle_t telDelayTimer = xTimerCreate(
            "TelDelay",
            pdMS_TO_TICKS(15000), // 15s offset
            pdFALSE,              // One-shot
            (void *)telemetryTimer,
            [](TimerHandle_t timer)
            {
                xTimerStart((TimerHandle_t)pvTimerGetTimerID(timer), 0);
                xTimerDelete(timer, 0);
            });
        if (telDelayTimer != nullptr)
        {
            xTimerStart(telDelayTimer, 0);
        }
        LOG_I(TAG, "Telemetry broadcast timer started (60s interval)");
    }

    // PeerNodeDB save timer (5 minutes)
    TimerHandle_t peerDbSaveTimer = xTimerCreate(
        "PeerDBSave",
        pdMS_TO_TICKS(300000), // 5 minutes
        pdTRUE,                // Auto-reload
        nullptr,
        [](TimerHandle_t)
        { PeerNodeDB::saveNow(); });
    if (peerDbSaveTimer != nullptr)
    {
        xTimerStart(peerDbSaveTimer, 0);
        LOG_I(TAG, "PeerNodeDB save timer started (5min interval)");
    }

    LOG_I(TAG, "Setup complete!");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop()
{
    Platform::resetWatchdog();

#ifdef LED_PIN
    ledManager->update();
#endif

    loraManager->process();

    // Process deferred config download (runs on main task stack, not nimble_host)
    bleManager->processPendingConfig();

    // Process BLE -> LoRa (ToRadio packets from phone)
    // All items enqueue quickly into TxQueue — none dropped
    meshtastic_ToRadio toRadio;
    while (bleToLoraQueue.pop(toRadio))
    {
        handleToRadioPacket(toRadio);
    }

    // Forward LoRa -> BLE (FromRadio packets to phone)
    meshtastic_FromRadio fromRadio;
    while (loraToBleQueue.pop(fromRadio))
    {
        if (bleManager->isConnected())
        {
            if (bleManager->sendFromRadio(&fromRadio))
            {
                resetInactivityTimer();
            }
            else
            {
                LOG_W(TAG, "Failed to send FromRadio to BLE");
            }
        }
        else
        {
            // Buffer received packets for when BLE reconnects
            if (fromRadio.which_payload_variant == meshtastic_FromRadio_packet_tag)
            {
                LOG_I(TAG, "BLE not connected, buffering FromRadio packet");
                storageManager->add(fromRadio);
            }
        }
    }

    // Send buffered messages when client reconnects
    if (bleManager->isConnected() && bleManager->areNotificationsEnabled() &&
        !storageManager->isEmpty())
    {
        meshtastic_FromRadio bufferedMsg;
        if (storageManager->peek(bufferedMsg))
        {
            if (bleManager->sendFromRadio(&bufferedMsg))
            {
                storageManager->popFront();
                LOG_I(TAG, "Sent buffered message, %d remaining", storageManager->getCount());
                resetInactivityTimer();
            }
        }
    }

    delay(20);
}

// ============================================================================
// Callbacks
// ============================================================================

void onBleConnected()
{
    LOG_I(TAG, "BLE connected");
    resetInactivityTimer();
    delay(500); // Wait for GATT stack setup
}

void onBleDisconnected()
{
    LOG_I(TAG, "BLE disconnected");
    bleManager->startAdvertising();
}

void onLoRaReceived(const LoRaPacket &packet)
{
    LOG_I(TAG, "LoRa packet received: %d bytes, RSSI: %d dBm, SNR: %.1f dB",
          packet.len, packet.rssi, packet.snr);

    lastRssi = packet.rssi;
    lastSnr = packet.snr;

#ifdef LED_PIN
    ledManager->blink(LEDConstants::RX_BLINKS);
#endif

    // Parse header first (needed for dedup check)
    MeshPacket::PacketHeader header;
    if (!MeshPacket::deserializeHeader(packet.buffer, header))
    {
        LOG_W(TAG, "Failed to deserialize packet header");
        return;
    }

    // Phase C: Extend pending TX timers by this packet's airtime
    // Avoids transmitting while we're still receiving on-air traffic
    uint32_t toaMs = (uint32_t)LoRaManager::calculateToA_ms(
        LoRaConstants::SPREADING_FACTOR,
        LoRaConstants::BANDWIDTH,
        LoRaConstants::CODING_RATE,
        LoRaConstants::PREAMBLE_LENGTH,
        packet.len);
    loraManager->extendPendingTimers(toaMs);

    // Phase D: Implicit ACK — check if this is our own packet being relayed
    uint32_t ownNodeNum = NodeDB::getOwnNodeNum();
    if (header.from == ownNodeNum)
    {
        if (checkImplicitAck(header.from, header.id))
        {
            LOG_I(TAG, "Implicit ACK: packet id=%lu relayed by another node",
                  (unsigned long)header.id);

            // Cancel any pending retries for this packet
            loraManager->cancelQueued(header.from, header.id);

            // Notify BLE app via FromRadio with ROUTING_APP ACK
            meshtastic_FromRadio ackFromRadio = meshtastic_FromRadio_init_zero;
            ackFromRadio.id = fromRadioId++;
            ackFromRadio.which_payload_variant = meshtastic_FromRadio_packet_tag;
            auto &ackPkt = ackFromRadio.packet;
            ackPkt.from = header.from;
            ackPkt.to = ownNodeNum;
            ackPkt.id = header.id;
            ackPkt.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            ackPkt.decoded.portnum = meshtastic_PortNum_ROUTING_APP;
            ackPkt.decoded.request_id = header.id;

            // Encode Routing ACK payload
            meshtastic_Routing routing = meshtastic_Routing_init_zero;
            routing.error_reason = meshtastic_Routing_Error_NONE;
            pb_ostream_t stream = pb_ostream_from_buffer(
                ackPkt.decoded.payload.bytes,
                sizeof(ackPkt.decoded.payload.bytes));
            if (pb_encode(&stream, meshtastic_Routing_fields, &routing))
            {
                ackPkt.decoded.payload.size = stream.bytes_written;
            }

            loraToBleQueue.push(ackFromRadio);
        }
        // Don't process our own relayed packet further
        return;
    }

    // Duplicate detection (before expensive decryption)
    if (isDuplicate(header.from, header.id))
    {
        // Phase E: Cancel our pending relay if another node already relayed
        int cancelled = loraManager->cancelQueuedRelays(header.from, header.id);
        if (cancelled > 0)
        {
            LOG_I(TAG, "Dupe cancellation: cancelled %d pending relay(s) for id=%lu",
                  cancelled, (unsigned long)header.id);
        }
        return;
    }

    // Mesh relay (operates on raw encrypted bytes, no decrypt needed)
    // Relay if: hopLimit > 0 (already excluded self above)
    if (header.getHopLimit() > 0)
    {
        // Copy raw packet and decrement hop_limit
        uint8_t relayPacket[MeshPacket::MAX_PACKET_SIZE];
        memcpy(relayPacket, packet.buffer, packet.len);

        // Hop limit is in bits 1-3 of byte 12 (flags field)
        uint8_t currentHopLimit = header.getHopLimit();
        uint8_t newHopLimit = currentHopLimit - 1;
        relayPacket[12] = (relayPacket[12] & 0xF1) | ((newHopLimit & 0x07) << 1);

        LOG_D(TAG, "Relay candidate: from=%08lx, hopLimit %u->%u",
              (unsigned long)header.from, currentHopLimit, newHopLimit);

        // Phase F: SNR-weighted delay
        scheduleRelay(relayPacket, packet.len, header.from, header.id, packet.snr);
    }

    // Multi-channel: find decryption key by channel hash
    uint8_t channelIdx;
    if (!MeshProtocol::findChannelByHash(header.channelHash, channelIdx))
    {
        LOG_W(TAG, "No matching channel for hash 0x%02X (relayed but not decoded)",
              header.channelHash);
        return;
    }
    uint8_t channelKey[32];
    MeshProtocol::getChannelKey(channelIdx, channelKey);

    // Parse Meshtastic packet (decrypt + decode protobuf)
    meshtastic_Data data;
    if (!MeshPacket::parsePacket(packet.buffer, packet.len, channelKey, header, data))
    {
        LOG_W(TAG, "Failed to parse LoRa packet");
        return;
    }

    // Record the sender
    NodeDB::recordSeenNode(header.from, packet.rssi, packet.snr);

    // Dispatch based on portnum
    switch (data.portnum)
    {
    case meshtastic_PortNum_POSITION_APP:
        AppHandlers::handlePositionApp(data, header.from);
        break;
    case meshtastic_PortNum_NODEINFO_APP:
        AppHandlers::handleNodeInfoApp(data, header.from);
        break;
    case meshtastic_PortNum_ROUTING_APP:
        AppHandlers::handleRoutingApp(data, header.from);
        break;
    case meshtastic_PortNum_TELEMETRY_APP:
        AppHandlers::handleTelemetryApp(data, header.from);
        break;
    case meshtastic_PortNum_ADMIN_APP:
        AppHandlers::handleAdminApp(data, header.from, header.id, AppHandlers::SOURCE_LORA);
        break;
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
        // Fall through to BLE forwarding
        break;
    default:
        LOG_D(TAG, "Unknown portnum: %d", data.portnum);
        // Still forward to BLE for app visibility
    }

    // Build a MeshPacket protobuf for the phone
    meshtastic_FromRadio fromRadio = meshtastic_FromRadio_init_zero;
    fromRadio.id = fromRadioId++;
    fromRadio.which_payload_variant = meshtastic_FromRadio_packet_tag;

    auto &meshPacket = fromRadio.packet;
    meshPacket.from = header.from;
    meshPacket.to = header.to;
    meshPacket.id = header.id;
    meshPacket.channel = header.getChannelIndex();
    meshPacket.hop_limit = header.getHopLimit();
    meshPacket.want_ack = header.getWantAck();
    uint32_t nowUnix = NodeDB::getCurrentTime();
    meshPacket.rx_time = (nowUnix > 0) ? nowUnix : (millis() / 1000);
    meshPacket.rx_snr = packet.snr;
    meshPacket.rx_rssi = packet.rssi;
    meshPacket.hop_start = header.getHopLimit();

    // Set decoded payload
    meshPacket.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    meshPacket.decoded = data;

    // ACK/NACK handling: only respond to unicast packets addressed to us
    if (header.getWantAck() && header.to == NodeDB::getOwnNodeNum())
    {
        if (bleManager->isConnected())
        {
            // Packet delivered to app — send ACK
            scheduleAckResponse(header.from, header.id);
        }
        else
        {
            // BLE not connected — we received it but can't deliver; send NACK
            LOG_D(TAG, "want_ack but BLE disconnected — sending NACK");
            scheduleAckResponse(header.from, header.id,
                                meshtastic_Routing_Error_NO_RESPONSE);
        }
    }

    if (loraToBleQueue.push(fromRadio))
    {
        LOG_D(TAG, "Queued FromRadio: from=%08lx, port=%d, %lu bytes",
              (unsigned long)header.from, data.portnum, (unsigned long)data.payload.size);
        resetInactivityTimer();
    }
    else
    {
        LOG_W(TAG, "LoRa->BLE queue full!");
    }
}

void onLoRaTransmitted(bool success)
{
    if (success)
    {
        LOG_I(TAG, "LoRa transmission successful");
#ifdef LED_PIN
        ledManager->blink(LEDConstants::TX_BLINKS);
#endif
    }
    else
    {
        LOG_W(TAG, "LoRa transmission failed");
    }
}

void handleToRadioPacket(const meshtastic_ToRadio &toRadio)
{
    if (toRadio.which_payload_variant != meshtastic_ToRadio_packet_tag)
    {
        LOG_W(TAG, "handleToRadioPacket called with non-packet variant: %lu",
              (unsigned long)toRadio.which_payload_variant);
        return;
    }

    const auto &meshPacket = toRadio.packet;
    LOG_D(TAG, "Processing ToRadio packet: to=%08lx, id=%lu",
          (unsigned long)meshPacket.to, (unsigned long)meshPacket.id);

    if (meshPacket.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
    {
        LOG_W(TAG, "ToRadio packet has no decoded payload");
        return;
    }

    const meshtastic_Data &data = meshPacket.decoded;

    // Intercept local admin requests (addressed to self)
    if (data.portnum == meshtastic_PortNum_ADMIN_APP &&
        meshPacket.to == NodeDB::getOwnNodeNum())
    {
        LOG_I(TAG, "Local admin request intercepted");
        AppHandlers::handleAdminApp(data, meshPacket.from, meshPacket.id, AppHandlers::SOURCE_BLE);
        return; // Don't transmit locally-addressed admin over LoRa
    }

    // Encode Data protobuf to plaintext
    uint8_t plaintext[MeshPacket::MAX_PAYLOAD_SIZE];
    pb_ostream_t stream = pb_ostream_from_buffer(plaintext, sizeof(plaintext));
    if (!pb_encode(&stream, meshtastic_Data_fields, &data))
    {
        LOG_E(TAG, "Failed to encode Data protobuf");
        return;
    }
    size_t plaintextLen = stream.bytes_written;

    // Generate packet ID if not provided
    uint32_t packetId = meshPacket.id;
    if (packetId == 0)
    {
        packetId = NodeDB::generatePacketId();
    }

    // Use our own node number as sender
    uint32_t fromNode = NodeDB::getOwnNodeNum();

    // Get channel key for the requested channel index
    uint8_t channelKey[32];
    MeshProtocol::getChannelKey(meshPacket.channel, channelKey);

    // Construct nonce and encrypt
    uint8_t nonce[16];
    MeshCrypto::constructNonce(packetId, fromNode, nonce);

    uint8_t ciphertext[MeshPacket::MAX_PAYLOAD_SIZE];
    if (!MeshCrypto::encrypt(plaintext, plaintextLen, channelKey, nonce, ciphertext))
    {
        LOG_E(TAG, "Encryption failed");
        return;
    }

    // Build OTA packet header
    MeshPacket::PacketHeader header;
    header.from = fromNode;
    header.to = meshPacket.to;
    header.id = packetId;
    header.flags = 0;
    header.setChannelIndex(meshPacket.channel);
    header.setHopLimit(meshPacket.hop_limit > 0 ? meshPacket.hop_limit : 3);
    header.setWantAck(meshPacket.want_ack);
    header.channelHash = MeshProtocol::getChannelHash(meshPacket.channel);
    header.reserved = 0;

    // Serialize complete OTA packet
    uint8_t otaBuffer[MeshPacket::MAX_PACKET_SIZE];
    MeshPacket::serializeHeader(header, otaBuffer);
    memcpy(otaBuffer + MeshPacket::HEADER_SIZE, ciphertext, plaintextLen);
    size_t totalLen = MeshPacket::HEADER_SIZE + plaintextLen;

    // Determine priority and retry count
    TxPriority priority = priorityFromPortnum(data.portnum);
    uint8_t maxRetries = maxRetriesForPriority(priority, meshPacket.want_ack);

    // Phase D: Record pending ACK for implicit ACK detection
    if (meshPacket.want_ack && meshPacket.to != MeshPacket::BROADCAST_ADDR)
    {
        recordPendingAck(fromNode, packetId);
    }

    // Queue for transmission (replaces direct startTransmit)
    if (loraManager->queueTransmit(otaBuffer, totalLen, priority,
                                   fromNode, packetId,
                                   false, // not a relay
                                   maxRetries))
    {
        LOG_I(TAG, "Queued MeshPacket: %lu bytes (to=%08lx, port=%d, pri=%u, retries=%u)",
              (unsigned long)totalLen, (unsigned long)meshPacket.to,
              data.portnum, priority, maxRetries);
    }
    else
    {
        LOG_W(TAG, "Failed to queue LoRa transmission");
    }
}
