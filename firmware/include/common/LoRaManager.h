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

/**
 * @file LoRaManager.h
 * @brief Unified LoRa radio manager for ESP32 and nRF52 platforms
 *
 * This class provides a high-level interface for LoRa communication,
 * abstracting RadioLib details and providing event-driven packet handling.
 * Works on both ESP32 and nRF52.
 */

/// State machine states for LoRa manager
enum LoRaState : uint8_t
{
    STATE_UNINITIALIZED,   // Radio not yet initialized
    STATE_IDLE,            // Initialized and ready (in RX mode)
    STATE_TRANSMITTING,    // Transmission in progress
    STATE_PACKET_RECEIVED, // Packet ready to read in process()
    STATE_PACKET_SENT      // Transmission completed, ready to process in process()
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

/// Callback type for received packets
using LoRaReceiveCallback = std::function<void(const LoRaPacket &packet)>;

/// Callback type for transmission completion
using LoRaTransmitCallback = std::function<void(bool success)>;

/**
 * @brief High-level manager for LoRa radio operations
 *
 * Responsibilities:
 *  - Initialize and configure LoRa radio with specified parameters
 *  - Transmit messages via LoRa with interrupt-driven approach
 *  - Receive packets using interrupt-driven approach
 *  - Provide event callbacks for received packets and transmission completion
 *  - Abstract RadioLib implementation details from application
 */
class LoRaManager
{
public:
    /**
     * @brief Construct LoRaManager with GPIO pin configuration
     * @param sck SPI clock pin
     * @param miso SPI MISO pin
     * @param mosi SPI MOSI pin
     * @param ss SPI slave select pin
     * @param rst Reset pin
     * @param dio0 DIO0 interrupt pin
     * @param busy Busy pin (for SX126x radios)
     */
    LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, int busy);

    /**
     * @brief Initialize LoRa radio
     * @return true on success, false on failure
     */
    bool begin();

    /**
     * @brief Start continuous receive mode or duty-cycled receive mode
     *
     * Uses hardware-based duty cycle mode
     * where the radio autonomously sleeps between RX windows to save power.
     *
     * @return true on success, false on failure
     */
    bool startReceive(bool dutyCycle = false);

    /**
     * @brief Start non-blocking interrupt-driven transmission
     *
     * This method initiates transmission and returns immediately.
     * The transmit callback will be invoked when transmission completes.
     * Automatically switches from RX -> TX, and back to RX after completion.
     *
     * @param data Pointer to data buffer
     * @param len Length of data to transmit
     * @param useLongPreamble Whether to use long preamble (2.5s) to wake sleeping receivers (default: true)
     * @return true if transmission started successfully, false otherwise
     */
    bool startTransmit(const uint8_t *data, size_t len, bool useLongPreamble = true);

    /**
     * @brief Check if transmission is in progress
     * @return true if transmitting, false otherwise
     */
    bool isTransmitting() const { return state == STATE_TRANSMITTING; }

    /**
     * @brief Set callback for received packets
     *
     * The callback will be invoked from the main loop (not ISR)
     * when a packet is successfully received.
     *
     * @param callback Function to call with received packet
     */
    void setReceiveCallback(LoRaReceiveCallback callback);

    /**
     * @brief Set callback for transmission completion
     *
     * The callback will be invoked from the main loop (not ISR)
     * when transmission completes (success or failure).
     *
     * @param callback Function to call with transmission result
     */
    void setTransmitCallback(LoRaTransmitCallback callback);

    /**
     * @brief Process LoRa events (call from main loop or task)
     *
     * Checks for received packets and transmission completion,
     * invoking callbacks as needed.
     * This should be called regularly from the main loop or FreeRTOS task.
     */
    void process();

    /**
     * @brief Get current RSSI of last received packet
     * @return RSSI in dBm
     */
    int getRSSI() const;

    /**
     * @brief Get current SNR of last received packet
     * @return SNR in dB
     */
    float getSNR() const;

    /**
     * @brief Check if LoRa radio is initialized
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const { return state != STATE_UNINITIALIZED; }

    /**
     * @brief Static ISR handler for LoRa DIO0 receive interrupt
     * Must be public to be registered as ISR callback
     */
    static void LORA_ISR_ATTR onReceiveISR();

    /**
     * @brief Static ISR handler for LoRa DIO0 transmit interrupt
     * Must be public to be registered as ISR callback
     */
    static void LORA_ISR_ATTR onTransmitISR();

    /**
     * @brief Calculates the Time on Air (ToA) for a LoRa packet.
     *
     * This function is based on the formulas provided in the Semtech datasheets.
     *
     * @param spreadingFactor The spreading factor (7-12).
     * @param bandwidth The bandwidth in Hz (e.g., 125000).
     * @param codingRate The coding rate (1-4, corresponding to 4/5 to 4/8).
     * @param preambleLength The number of preamble symbols.
     * @param payloadLength The length of the payload in bytes.
     * @param explicitHeader True if an explicit header is used, false for implicit.
     * @param crcEnabled True if CRC is enabled.
     * @param lowDataRateOptimize True if low data rate optimization is enabled.
     * @return The Time on Air in milliseconds.
     */
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
    int pinBusy; // For SX126x radios

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
};

#endif // LORA_MANAGER_H
