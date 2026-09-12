#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include <RadioLib.h>
#include <functional>
#include <common/Protocol.h>

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

class LoRaManager
{
public:
    // rfSwitch: external RXEN pin for boards with an RF switch stage beyond
    // the radio's internal DIO2 switch (e.g. Wio-SX1262), or -1 if unused
    LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, int busy, int rfSwitch = -1);

    // Non-copyable (singleton with dynamic allocation)
    LoRaManager(const LoRaManager&) = delete;
    LoRaManager& operator=(const LoRaManager&) = delete;

    // Must be called on cold start; use handleSleepWakeup() instead after a LoRa wakeup
    bool begin();

    // Reads a pending packet without resetting the radio, for deep-sleep LoRa wakeups
    bool handleSleepWakeup();

    bool startReceive(bool dutyCycle = false);

    // Internal use only; queueTransmit() is the public TX entry point (see FirmwareConfig.h notes)
    bool startTransmit(const uint8_t *data, size_t len);

    // Public TX API: enqueues for CAD-based transmission (sends once the channel is free)
    bool queueTransmit(const uint8_t *data, size_t len);

    bool isTransmitting() const { return state == STATE_TRANSMITTING; }

    // Invoked from the main loop, not the ISR
    void setReceiveCallback(LoRaReceiveCallback callback);

    // Invoked from the main loop, not the ISR
    void setTransmitCallback(LoRaTransmitCallback callback);

    // Call regularly from the main loop or FreeRTOS task
    void process();

    int getRSSI() const;

    float getSNR() const;

    bool isInitialized() const { return state != STATE_UNINITIALIZED; }

    // Must be public to be registered as the DIO0 receive ISR callback
    static void LORA_ISR_ATTR onReceiveISR();

    // Must be public to be registered as the DIO0 transmit ISR callback
    static void LORA_ISR_ATTR onTransmitISR();

    // Time on Air per the Semtech LoRa modem datasheet formulas; codingRate is 1-4 for CR 4/5-4/8
    static inline double calculateToA_ms(
        uint8_t spreadingFactor,
        double bandwidth,
        uint8_t codingRate,
        uint16_t preambleLength,
        uint8_t payloadLength,
        bool explicitHeader = true,
        bool crcEnabled = true)
    {
        // Symbol duration (bandwidth parameter is in kHz)
        double t_sym = std::pow(2, spreadingFactor) / (bandwidth);

        bool lowDataRateOptimize = t_sym >= 16.0;
        // Preamble duration
        double t_preamble = (preambleLength + 4.25) * t_sym;

        // Payload number of symbols
        int8_t header = explicitHeader ? 0 : 1;
        int8_t crc = crcEnabled ? 16 : 0;
        int8_t de = lowDataRateOptimize ? 1 : 0;

        double payload_numerator = 8.0 * payloadLength - 4.0 * spreadingFactor + 28.0 + crc - 20.0 * header;
        double payload_denominator = 4.0 * (spreadingFactor - 2.0 * de);

        double n_payload = 8.0 + std::max(0.0, std::ceil(payload_numerator / payload_denominator) * (codingRate + 4.0));

        // Payload duration
        double t_payload = n_payload * t_sym;

        // Total ToA
        return t_preamble + t_payload;
    }

private:
    // GPIO pin configuration
    int pinSCK;
    int pinMISO;
    int pinMOSI;
    int pinSS;
    int pinRST;
    int pinDIO0;
    int pinBusy;    // For SX126x radios
    int pinRfSwitch; // External RXEN pin, or -1 if unused (Wio-SX1262 style boards)

    // RadioLib Module instance (kept for direct access during wakeup)
    Module *module;

    // RadioLib radio instance (type depends on RADIO_ definition)
#if defined(RADIO_SX1262)
    SX1262 *radio;
#elif defined(RADIO_SX1268)
    SX1268 *radio;
#else
#error "No supported RADIO defined! Please define RADIO_SX1262, or RADIO_SX1268"
#endif

    // State machine
    volatile LoRaState state;

    // Callbacks
    LoRaReceiveCallback receiveCallback;
    LoRaTransmitCallback transmitCallback;

    // Singleton instance for ISR access
    static LoRaManager *instance;

    // Helper to initialize SPI bus
    void initSPI();

    // Internal TX queue (CAD-based transmission)
    static constexpr int TX_QUEUE_SIZE = 5;
    TxPacket txQueue[TX_QUEUE_SIZE];
    int txQueueHead = 0;
    int txQueueTail = 0;
    int txQueueCount = 0;
    int cadRetries = 0;

    // TX→RX settle deadline (millis timestamp, used in STATE_TX_SETTLING)
    uint32_t txSettleDeadline = 0;

    /// Process TX queue: CAD check then transmit if channel free
    bool processTxQueue();
};

#endif // LORA_MANAGER_H
