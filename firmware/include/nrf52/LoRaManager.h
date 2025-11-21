#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include <RadioLib.h>
#include <functional>
#include "Protocol.h"

/**
 * @file LoRaManager.h
 * @brief Encapsulates LoRa radio operations and message handling for nRF52
 *
 * This class provides a high-level interface for LoRa communication,
 * abstracting RadioLib details and providing event-driven packet handling.
 * Compatible with ESP32 version for protocol compatibility.
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
struct LoRaConfig
{
    float frequency;         // MHz (e.g., 433.92)
    float bandwidth;         // kHz (e.g., 250)
    uint8_t spreadingFactor; // SF (7-12)
    uint8_t codingRate;      // CR (5-8 for 4/5 to 4/8)
    int8_t txPower;          // dBm (e.g., 20)
};

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
 * @brief High-level manager for LoRa radio operations on nRF52
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
     * @param busy Busy pin (for SX1262 radios)
     */
    LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, int busy);

    /**
     * @brief Initialize LoRa radio with configuration
     * @param config LoRa parameters (frequency, SF, BW, etc.)
     * @return true on success, false on failure
     */
    bool begin(const LoRaConfig &config);

    /**
     * @brief Start continuous receive mode or duty-cycled receive mode
     *
     * For SX1262: Uses hardware-based duty cycle mode
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
     * @return true if transmission started successfully, false otherwise
     */
    bool startTransmit(const uint8_t *data, size_t len);

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
    static void onReceiveISR();

    /**
     * @brief Static ISR handler for LoRa DIO0 transmit interrupt
     * Must be public to be registered as ISR callback
     */
    static void onTransmitISR();

private:
    // GPIO pin configuration
    int pinSCK;
    int pinMISO;
    int pinMOSI;
    int pinSS;
    int pinRST;
    int pinDIO0;
    int pinBusy; // For SX1262 radios

    // RadioLib radio instance (SX1262 for nRF52)
#if defined(RADIO_SX1262)
    SX1262 *radio;
#else
#error "Only SX1262 is supported for nRF52 firmware"
#endif

    // State machine
    volatile LoRaState state;

    // ISR tracking
    volatile uint32_t rxInterruptCount;
    volatile uint32_t txInterruptCount;
    uint32_t rxProcessedCount;
    uint32_t txProcessedCount;

    // Callbacks
    LoRaReceiveCallback receiveCallback;
    LoRaTransmitCallback transmitCallback;

    // Singleton instance for ISR access
    static LoRaManager *instance;
};

#endif // LORA_MANAGER_H
