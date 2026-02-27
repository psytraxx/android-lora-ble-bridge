#ifndef FIRMWARE_CONFIG_H
#define FIRMWARE_CONFIG_H

#include <cstdint>
#include <cstring>
#include "common/LoRaManager.h"

/**
 * @file FirmwareConfig.h
 * @brief Unified configuration for LoRa-BLE Bridge firmware (ESP32 and nRF52)
 *
 * This header consolidates all firmware constants, timeouts, and configuration
 * in one location for easy modification and documentation.
 *
 * Design Goals:
 *  - Single source of truth for configuration values
 *  - Platform-specific values handled via conditional compilation
 *  - Self-documenting with clear comments
 *  - Type-safe constants instead of preprocessor macros
 *  - Easy to port to runtime configuration (JSON/EEPROM) later
 */

//==============================================================================
// GPIO Pin Configuration
//==============================================================================

/// GPIO pin configuration for peripherals
/// Note: These are defined as build-time constants via platformio.ini
/// for hardware portability. Access via LORA_*, LED_PIN, WAKE_BUTTON macros.

// LoRa SPI pins - defined in platformio.ini:
// LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS, LORA_RST, LORA_DIO0, LORA_BUSY

// LED status indicator - defined in platformio.ini:
// LED_PIN (optional)

// Wake button - defined in platformio.ini:
// WAKE_BUTTON (optional)

//==============================================================================
// LoRa Radio Configuration
//==============================================================================

namespace LoRaConstants
{
    constexpr float FREQUENCY = 433.92; ///< LoRa frequency in MHz

    constexpr float BANDWIDTH = 125.0; ///< LoRa bandwidth in kHz (250 = faster data rate, moderate range)

    constexpr uint8_t SPREADING_FACTOR = 11; ///< LoRa spreading factor (7-12, higher = longer range, slower)

    constexpr uint8_t CODING_RATE = 7; ///< LoRa coding rate (5=4/5, 6=4/6, 7=4/7, 8=4/8, higher = better error correction)

    /// Default LoRa sync word (0x12 = private network, 0x34 = public LoRaWAN)
    constexpr uint8_t SYNC_WORD = 0x12;

    /// Time to wait for radio hardware to settle after mode change (TX/RX switch)
    constexpr int RX_SETTLE_TIME_MS = 50;

    /// Safety margin added to Time-on-Air calculations to ensure reliability
    constexpr int TIMING_MARGIN_MS = 300;

    /// Estimated deep sleep wake time in milliseconds
    constexpr int DEEP_SLEEP_WAKE_TIME_MS = 600;

    /// Preamble length for all LoRa transmissions
    /// Set to 64 symbols (~525ms at SF11/BW250) to ensure reliable duty-cycle RX detection
    /// and allow the text message itself to wake sleeping receivers (no separate WakeUp needed)
    constexpr int PREAMBLE_LENGTH = 64;

    /// CAD (Channel Activity Detection) configuration
    /// Used to check if the channel is free before transmitting
    constexpr int CAD_MAX_RETRIES = 5;        ///< Max CAD attempts before force-transmitting
    constexpr int CAD_BACKOFF_BASE_MS = 50;   ///< Base backoff between CAD retries
    constexpr int CAD_BACKOFF_JITTER_MS = 100; ///< Random jitter added to backoff

    /// Number of retry attempts for LoRa initialization
    constexpr int INIT_RETRY_COUNT = 3;

    /// Delay between LoRa initialization retries (milliseconds)
    constexpr int INIT_RETRY_DELAY_MS = 200;

    /// LoRa TX power from build flags (dBm)
    constexpr int TX_POWER = LORA_TX_POWER;

    /// tcxoVoltage for SX1262/SX1268 radios (1.6V, 1.7V, 1.8V, 2.2V, 2.4V, 2.7V, 3.0V, 3.3V)
    constexpr float TCXO_VOLTAGE = 1.8;
}

//==============================================================================
// Device Info BLE Characteristic Data (16 bytes)
//==============================================================================

/// Data structure for the Device Info BLE characteristic
/// Populated on-demand when the client reads the characteristic
struct DeviceInfoData
{
    uint8_t batteryLevel;    // 0-100%
    int16_t rssi;            // Last LoRa RSSI in dBm
    int16_t snrX100;         // Last LoRa SNR × 100 (e.g., 975 = 9.75 dB)
    int8_t txPower;          // LoRa TX power in dBm
    uint32_t frequencyHz;    // LoRa frequency in Hz
    uint32_t bandwidthHz;    // LoRa bandwidth in Hz
    uint8_t spreadingFactor; // 7-12
    uint8_t codingRate;      // 5-8

    /// Serialize to 16-byte little-endian buffer for BLE characteristic
    void serialize(uint8_t *buf) const
    {
        buf[0] = batteryLevel;
        std::memcpy(buf + 1, &rssi, 2);
        std::memcpy(buf + 3, &snrX100, 2);
        buf[5] = static_cast<uint8_t>(txPower);
        std::memcpy(buf + 6, &frequencyHz, 4);
        std::memcpy(buf + 10, &bandwidthHz, 4);
        buf[14] = spreadingFactor;
        buf[15] = codingRate;
    }
};

//==============================================================================
// BLE Configuration
//==============================================================================

namespace BLEConstants
{
    /// BLE service UUID (application-specific)
    constexpr const char *SERVICE_UUID = "00001234-0000-1000-8000-00805f9b34fb";

    /// BLE TX characteristic UUID (Device -> Android notifications)
    constexpr const char *TX_CHARACTERISTIC_UUID = "00005678-0000-1000-8000-00805f9b34fb";

    /// BLE RX characteristic UUID (Android -> Device writes)
    constexpr const char *RX_CHARACTERISTIC_UUID = "00005679-0000-1000-8000-00805f9b34fb";

    /// BLE Device Info characteristic UUID (read-only, 16 bytes)
    /// Returns: [Battery:1][RSSI:2 LE][SNR:2 LE][TxPower:1][Freq:4 LE][BW:4 LE][SF:1][CR:1]
    constexpr const char *INFO_CHARACTERISTIC_UUID = "0000567a-0000-1000-8000-00805f9b34fb";

    /// Minimum BLE advertising interval (in 0.625ms units)
    /// 1600 * 0.625ms = 1000ms (1 second)
    constexpr int ADV_MIN_INTERVAL = 1600;

    /// Maximum BLE advertising interval (in 0.625ms units)
    /// 3200 * 0.625ms = 2000ms (2 seconds)
    constexpr int ADV_MAX_INTERVAL = 3200;

#if defined(ARDUINO_ARCH_ESP32)
    /// BLE TX power level for balance of range (~10m) vs power consumption
    /// ESP32 options: P9 (+9dBm max), P6 (+6dBm), P3 (+3dBm balanced), P0 (0dBm), N3 (-3dBm min)
    constexpr int TX_POWER_LEVEL = 6; // ESP_PWR_LVL_P6
#elif defined(ARDUINO_ARCH_NRF52)
    /// BLE TX power level (nRF52: -40, -20, -16, -12, -8, -4, 0, +3, +4 dBm)
    constexpr int TX_POWER_DBM = 0; // 0 dBm for balanced range/power
#endif
}

//==============================================================================
// Message Buffer Configuration
//==============================================================================

namespace BufferConstants
{
    /// Maximum messages to buffer when BLE disconnected
    /// Each Message is ~160 bytes, so 10 messages = ~1.6 KB RAM
    constexpr int MAX_BUFFERED_MESSAGES = 10;

    /// Maximum LoRa payload size (RadioLib limit)
    constexpr int MAX_LORA_PAYLOAD = 256;

    /// Maximum protocol message size (ACK=2, Text+GPS=52)
    constexpr int MAX_PROTOCOL_MESSAGE = 64;
}

//==============================================================================
// Power Management Configuration
//==============================================================================

namespace PowerConstants
{
    /// Inactivity timeout before entering deep sleep (milliseconds)
    /// Device enters deep sleep after this duration with no BLE or LoRa activity
    /// BLE advertising continues until device is connected or enters deep sleep
    constexpr unsigned long INACTIVITY_TIMEOUT_MS = 60000UL;

#if defined(ARDUINO_ARCH_NRF52)
    /// Battery voltage divider ratio - defined in platformio.ini:
    /// BATTERY_VOLTAGE_DIVIDER
    constexpr float BATTERY_DIVIDER = BATTERY_VOLTAGE_DIVIDER;

    /// ADC maximum input voltage for nRF52840
    /// Uses AR_INTERNAL_3_0: 0.6V reference × 5 = 3.0V max measurable range
    /// This provides better accuracy for LiPo batteries (3.0V-4.2V after voltage divider)
    constexpr float ADC_MAX_VOLTAGE = 3.0; // Volts

    /// ADC resolution bits (nRF52840 supports 8, 10, 12, 14-bit)
    constexpr int ADC_RESOLUTION_BITS = 12; // 12-bit = 0-4095
#endif
}

/// Number of cells in battery pack (1 for single-cell Li-ion)
constexpr int NUM_CELLS = 1;

/// Number of points in OCV lookup table
constexpr int NUM_OCV_POINTS = 11;

/// OCV (Open Circuit Voltage) lookup table for Li-ion battery (millivolts per cell)
/// Index 0 = 100% charged, Index 10 = 0% charged
/// Based on typical single-cell Li-ion discharge curve
/// Source: Meshtastic project (validated across many devices)
const uint16_t OCV[NUM_OCV_POINTS] = {
    4200, // 100% - Fully charged
    4050, // 90%
    3900, // 80%
    3800, // 70%
    3730, // 60%
    3680, // 50%
    3630, // 40%
    3570, // 30%
    3500, // 20%
    3400, // 10%
    3100  // 0% - Cut-off voltage
};

//==============================================================================
// Watchdog Timer Configuration
//==============================================================================

namespace WatchdogConstants
{
    /// Watchdog timeout in seconds
    /// LoRa TX at SF11+BW31kHz can take 2-3s, so 10s provides safe margin
    constexpr int TIMEOUT_SECONDS = 10;
}

//==============================================================================
// LED Configuration
//==============================================================================

namespace LEDConstants
{
    /// Delay between consecutive blinks (milliseconds)
    constexpr int BLINK_DELAY_MS = 200;

    /// Number of blinks for TX event
    constexpr int TX_BLINKS = 2;

    /// Number of blinks for RX event
    constexpr int RX_BLINKS = 1;

    /// Heartbeat interval (milliseconds) - regular status indication
    constexpr unsigned long HEARTBEAT_INTERVAL_MS = 2000;

    /// Heartbeat blink duration (milliseconds) - brief flash
    constexpr int HEARTBEAT_DURATION_MS = 5;
}

#endif // FIRMWARE_CONFIG_H
