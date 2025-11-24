#ifndef FIRMWARE_CONFIG_H
#define FIRMWARE_CONFIG_H

#include <cstdint>
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

    constexpr float BANDWIDTH = 250.0; ///< LoRa bandwidth in kHz

    constexpr uint8_t SPREADING_FACTOR = 9; ///< LoRa spreading factor (7-12)

    constexpr uint8_t CODING_RATE = 5; ///< LoRa coding rate (5=4/5, 6=4/6, 7=4/7, 8=4/8)

    /// Default LoRa sync word (0x12 = private network, 0x34 = public LoRaWAN)
    constexpr uint8_t SYNC_WORD = 0x12;

    /// Time to wait for radio hardware to settle after mode change (TX/RX switch)
    constexpr int RX_SETTLE_TIME_MS = 50;

    /// Safety margin added to Time-on-Air calculations to ensure reliability
    constexpr int TIMING_MARGIN_MS = 500;

    /// Preamble length for WakeUp messages to wake duty-cycled receivers
    constexpr int PREAMBLE_LENGTH = 16;

    /// Delay after sending WakeUp message before sending actual message.
    /// Calculated as: ToA(WakeUp) + Deep Sleep Wake Time + RX Settle Time + Margin
    /// This ensures the receiver has ample time to wake up and switch to RX mode.
    const int WAKEUP_TO_MESSAGE_DELAY_MS =
        static_cast<int>(LoRaManager::calculateToA_ms(
            LoRaConstants::SPREADING_FACTOR,
            LoRaConstants::BANDWIDTH * 1000,
            LoRaConstants::CODING_RATE,
            LoRaConstants::PREAMBLE_LENGTH,
            1)) +
        600 + // Estimated deep sleep wake time
        RX_SETTLE_TIME_MS +
        TIMING_MARGIN_MS;

    /// Delay before sending an ACK to ensure the original sender has switched to RX mode.
    /// Calculated as: ToA(Max_Message) + TX->RX Switch Time + Margin
    const int ACK_DELAY_MS =
        static_cast<int>(LoRaManager::calculateToA_ms(
            LoRaConstants::SPREADING_FACTOR,
            LoRaConstants::BANDWIDTH * 1000,
            LoRaConstants::CODING_RATE,
            LoRaConstants::PREAMBLE_LENGTH,
            64)) + // Max expected payload
        RX_SETTLE_TIME_MS +
        TIMING_MARGIN_MS;

    /// Number of retry attempts for LoRa initialization
    constexpr int INIT_RETRY_COUNT = 3;

    /// Delay between LoRa initialization retries (milliseconds)
    constexpr int INIT_RETRY_DELAY_MS = 1000;

    /// tcxoVoltage for SX1262/SX1268 radios (1.6V, 1.7V, 1.8V, 2.2V, 2.4V, 2.7V, 3.0V, 3.3V)
    constexpr float TCXO_VOLTAGE = 1.8;
}

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

    /// Standard BLE Battery Service UUID (read-only)
    constexpr const char *BATTERY_SERVICE_UUID = "0000180f-0000-1000-8000-00805f9b34fb";

    /// Standard BLE Battery Level Characteristic UUID (uint8, 0-100%)
    constexpr const char *BATTERY_LEVEL_UUID = "00002a19-0000-1000-8000-00805f9b34fb";

    /// Minimum BLE advertising interval (in 0.625ms units)
    /// 1600 * 0.625ms = 1000ms (1 second)
    constexpr int ADV_MIN_INTERVAL = 1600;

    /// Maximum BLE advertising interval (in 0.625ms units)
    /// 3200 * 0.625ms = 2000ms (2 seconds)
    constexpr int ADV_MAX_INTERVAL = 3200;

#if defined(ARDUINO_ARCH_ESP32)
    /// BLE TX power level for balance of range (~10m) vs power consumption
    /// ESP32 options: P9 (+9dBm max), P6 (+6dBm), P3 (+3dBm balanced), P0 (0dBm), N3 (-3dBm min)
    constexpr int TX_POWER_LEVEL = 3; // ESP_PWR_LVL_P3
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
    /// Uses internal 0.6V reference with 1/6 gain = 0.6V * 6 = 3.6V max measurable range
    constexpr float ADC_MAX_VOLTAGE = 3.6; // Volts

    /// ADC resolution bits (nRF52840 supports 8, 10, 12, 14-bit)
    constexpr int ADC_RESOLUTION_BITS = 12; // 12-bit = 0-4095
#endif
}

//==============================================================================
// Battery Monitoring Configuration
//==============================================================================

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
}

#endif // FIRMWARE_CONFIG_H
