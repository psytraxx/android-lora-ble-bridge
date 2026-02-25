#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include <RadioLib.h>
#include <functional>
#include "common/TxQueue.h"

// Platform-specific includes and definitions
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_attr.h>
#define LORA_ISR_ATTR IRAM_ATTR
#elif defined(ARDUINO_ARCH_NRF52)
#define LORA_ISR_ATTR
#else
#error "Unsupported platform"
#endif

/// State machine states for LoRa manager
enum LoRaState : uint8_t
{
    STATE_UNINITIALIZED,   // Radio not yet initialized
    STATE_IDLE,            // Initialized and ready (in RX mode)
    STATE_TRANSMITTING,    // Transmission in progress
    STATE_PACKET_RECEIVED, // Packet ready to read in process()
    STATE_PACKET_SENT,     // Transmission completed, ready to process in process()
    STATE_TX_SETTLING      // Brief non-blocking settle period after TX before switching to RX
};

/// Configuration for LoRa radio parameters
#ifndef LORA_CONFIG_DEFINED
#define LORA_CONFIG_DEFINED

/// LoRa packet with metadata (RSSI, SNR)
struct LoRaPacket
{
    uint8_t buffer[256]; // 256 bytes = max LoRa payload (RadioLib limit)
    int len;             // Actual packet length
    int rssi;            // Received Signal Strength Indicator (dBm)
    float snr;           // Signal-to-Noise Ratio (dB)
};
#endif

/// TX packet for internal queue (holds serialized bytes ready to transmit)
struct TxPacket
{
    uint8_t data[64]; // MAX_PROTOCOL_MESSAGE
    size_t len;
};

/// Callback type for received packets
using LoRaReceiveCallback = std::function<void(const LoRaPacket &packet)>;

/// Callback type for transmission completion
using LoRaTransmitCallback = std::function<void(bool success)>;

/**
 * @brief High-level manager for LoRa radio operations
 *
 * Responsibilities:
 *  - Initialize and configure LoRa radio with specified parameters
 *  - Queue and prioritize outgoing transmissions
 *  - CSMA/CA channel sensing before transmit
 *  - Retry management for unacked packets
 *  - Receive packets using interrupt-driven approach
 *  - Provide event callbacks for received packets and transmission completion
 */
class LoRaManager
{
public:
    LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, int busy);

    // Non-copyable
    LoRaManager(const LoRaManager&) = delete;
    LoRaManager& operator=(const LoRaManager&) = delete;

    bool begin();
    bool handleSleepWakeup();
    bool startReceive(bool dutyCycle = false);

    /**
     * @brief Queue a packet for transmission with priority ordering
     *
     * Replaces direct startTransmit() calls. Packets are stored in a
     * priority queue and transmitted from process() when the channel is clear.
     *
     * @param data Packet data to transmit
     * @param len Length of data
     * @param priority Transmission priority
     * @param fromNode Source node number (for cancellation matching)
     * @param packetId Packet ID (for cancellation matching)
     * @param isRelay True if this is a mesh relay
     * @param maxRetries Number of retry attempts (0 = no retry)
     * @param delayMs Minimum delay before first TX attempt
     * @return true if queued successfully
     */
    bool queueTransmit(const uint8_t *data, size_t len, TxPriority priority,
                       uint32_t fromNode, uint32_t packetId,
                       bool isRelay = false, uint8_t maxRetries = 0,
                       uint32_t delayMs = 0);

    /**
     * @brief Cancel all queued entries matching fromNode + packetId
     */
    int cancelQueued(uint32_t fromNode, uint32_t packetId);

    /**
     * @brief Cancel only relay entries matching fromNode + packetId
     */
    int cancelQueuedRelays(uint32_t fromNode, uint32_t packetId);

    /**
     * @brief Extend TX timers for all pending entries (collision avoidance)
     */
    void extendPendingTimers(uint32_t ms);

    bool isTransmitting() const { return state == STATE_TRANSMITTING; }

    void setReceiveCallback(LoRaReceiveCallback callback);
    void setTransmitCallback(LoRaTransmitCallback callback);

    /**
     * @brief Process LoRa events and dequeue TX packets
     *
     * Handles RX packet delivery, TX completion, and dequeues the next
     * ready packet with CSMA/CA channel sensing.
     */
    void process();

    int getRSSI() const;
    float getSNR() const;
    bool isInitialized() const { return state != STATE_UNINITIALIZED; }

    static void LORA_ISR_ATTR onReceiveISR();
    static void LORA_ISR_ATTR onTransmitISR();

    static inline double calculateToA_ms(
        uint8_t spreadingFactor,
        double bandwidth,
        uint8_t codingRate,
        uint16_t preambleLength,
        uint8_t payloadLength,
        bool explicitHeader = true,
        bool crcEnabled = true)
    {
        double t_sym = std::pow(2, spreadingFactor) / (bandwidth);
        bool lowDataRateOptimize = t_sym >= 16.0;
        double t_preamble = (preambleLength + 4.25) * t_sym;

        int8_t header = explicitHeader ? 0 : 1;
        int8_t crc = crcEnabled ? 16 : 0;
        int8_t de = lowDataRateOptimize ? 1 : 0;

        double payload_numerator = 8.0 * payloadLength - 4.0 * spreadingFactor + 28.0 + crc - 20.0 * header;
        double payload_denominator = 4.0 * (spreadingFactor - 2.0 * de);
        double n_payload = 8.0 + std::max(0.0, std::ceil(payload_numerator / payload_denominator) * (codingRate + 4.0));
        double t_payload = n_payload * t_sym;

        return t_preamble + t_payload;
    }

private:
    // GPIO pin configuration
    int pinSCK, pinMISO, pinMOSI, pinSS, pinRST, pinDIO0, pinBusy;

    Module *module;

#if defined(RADIO_SX1262)
    SX1262 *radio;
#elif defined(RADIO_SX1268)
    SX1268 *radio;
#else
#error "No supported RADIO defined! Please define RADIO_SX1262, or RADIO_SX1268"
#endif

    volatile LoRaState state;

    LoRaReceiveCallback receiveCallback;
    LoRaTransmitCallback transmitCallback;

    static LoRaManager *instance;

    void initSPI();

    // TX queue and current transmission tracking
    TxQueue txQueue_;
    TxQueueEntry *currentTxEntry_;

    // CSMA/CA state
    uint8_t csmaBackoffCount_;

    /**
     * @brief Start non-blocking interrupt-driven transmission (internal use)
     *
     * Called only from process() after CSMA check passes.
     */
    bool startTransmit(const uint8_t *data, size_t len);

    /**
     * @brief Check if the radio channel is clear using CAD
     * @return true if channel is free, false if activity detected
     */
    bool isChannelClear();
};

#endif // LORA_MANAGER_H
