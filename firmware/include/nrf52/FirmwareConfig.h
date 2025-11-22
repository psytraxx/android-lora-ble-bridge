#ifndef FIRMWARE_CONFIG_H
#define FIRMWARE_CONFIG_H

#include <cstdint>

//==============================================================================
// LoRa Radio Configuration (Must match ESP32 for interoperability)
//==============================================================================

// LoRa radio parameters - defined in platformio.ini:
// LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODING_RATE, LORA_TX_POWER, RADIO_SX1262

namespace LoRaConstants
{
    /// Default LoRa sync word (0x12 = private network, 0x34 = public LoRaWAN)
    constexpr uint8_t SYNC_WORD = 0x12;

    /// Time to wait for radio hardware to settle after mode change (TX/RX switch)
    constexpr int RX_SETTLE_TIME_MS = 50;

    /// Safety margin added to Time-on-Air calculations to ensure reliability
    constexpr int TIMING_MARGIN_MS = 500;

    /// Preamble length for WakeUp messages to wake duty-cycled receivers
    constexpr int PREAMBLE_LENGTH = 16;

    /// Delay after sending WakeUp message before sending actual message.
    /// This ensures the receiver has ample time to wake up and switch to RX mode.
    constexpr int WAKEUP_TO_MESSAGE_DELAY_MS = 3000; // Simplified for now

    /// Delay before sending an ACK to ensure the original sender has switched to RX mode.
    constexpr int ACK_DELAY_MS = 2000; // Simplified for now

    /// Number of retry attempts for LoRa initialization
    constexpr int INIT_RETRY_COUNT = 3;

    /// Delay between LoRa initialization retries (milliseconds)
    constexpr int INIT_RETRY_DELAY_MS = 1000;

    /// tcxoVoltage for SX1262 radios (1.6V, 1.7V, 1.8V, 2.2V, 2.4V, 2.7V, 3.0V, 3.3V)
    constexpr float TCXO_VOLTAGE = 1.8;

    /// Use DIO2 for RF switch control (SX1262 feature)
    constexpr bool USE_DIO2_AS_RF_SWITCH = true;
}

//==============================================================================
// BLE Configuration (Must match ESP32 for protocol compatibility)
//==============================================================================

namespace BLEConstants
{
    /// BLE service UUID (application-specific, must match ESP32)
    constexpr const char *SERVICE_UUID = "00001234-0000-1000-8000-00805f9b34fb";

    /// BLE TX characteristic UUID (nRF52 -> Android notifications)
    constexpr const char *TX_CHARACTERISTIC_UUID = "00005678-0000-1000-8000-00805f9b34fb";

    /// BLE RX characteristic UUID (Android -> nRF52 writes)
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

    /// BLE TX power level (nRF52: -40, -20, -16, -12, -8, -4, 0, +3, +4 dBm)
    constexpr int TX_POWER_DBM = 0; // 0 dBm for balanced range/power

    /// Number of retry attempts for BLE initialization
    constexpr int INIT_RETRY_COUNT = 3;

    /// Delay between BLE initialization retries (milliseconds)
    constexpr int INIT_RETRY_DELAY_MS = 1000;

    /// Time to wait after disconnect before restarting advertising
    constexpr int DISCONNECT_SETTLE_MS = 300;

    /// Spacing between consecutive BLE message sends to avoid overwhelming stack
    constexpr int MESSAGE_SPACING_MS = 500;
}

//==============================================================================
// Message Queue Configuration
//==============================================================================

namespace QueueConstants
{
    /// BLE -> LoRa queue size (lower since app sends one at a time)
    constexpr int BLE_TO_LORA_SIZE = 10;

    /// LoRa -> BLE queue size (higher to handle burst reception from multiple senders)
    constexpr int LORA_TO_BLE_SIZE = 15;
}

//==============================================================================
// Message Buffer Configuration
//==============================================================================

namespace BufferConstants
{
    /// Maximum messages to buffer when BLE disconnected
    /// Each Message is ~160 bytes, so 10 messages = ~1.6 KB RAM
    constexpr int MAX_BUFFERED_MESSAGES = 10;

    /// Maximum LoRa payload size (RadioLib SX1262 limit)
    constexpr int MAX_LORA_PAYLOAD = 256;

    /// Maximum protocol message size (ACK=2, Text+GPS=52)
    constexpr int MAX_PROTOCOL_MESSAGE = 64;
}

//==============================================================================
// Power Management Configuration (nRF52-specific)
//==============================================================================

namespace PowerConstants
{
    /// BLE advertising duration before entering low-power mode (milliseconds)
    /// 30 seconds provides good discoverability window while conserving power
    constexpr unsigned long ADVERTISE_DURATION_MS = 30000UL;

    /// BLE connection inactivity timeout before forced disconnect (milliseconds)
    /// 60 seconds allows for casual message reading without premature disconnection
    constexpr unsigned long INACTIVITY_TIMEOUT_MS = 60000UL;

    /// Battery voltage divider ratio - defined in platformio.ini:
    /// BATTERY_VOLTAGE_DIVIDER
    constexpr float BATTERY_DIVIDER = BATTERY_VOLTAGE_DIVIDER;

    /// ADC maximum input voltage for nRF52840
    /// Uses internal 0.6V reference with 1/6 gain = 0.6V * 6 = 3.6V max measurable range
    constexpr float ADC_MAX_VOLTAGE = 3.6; // Volts

    /// ADC resolution bits (nRF52840 supports 8, 10, 12, 14-bit)
    constexpr int ADC_RESOLUTION_BITS = 12; // 12-bit = 0-4095
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
// Watchdog Configuration
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
    /// LED blink duration for single event (milliseconds)
    constexpr int BLINK_DURATION_MS = 50;

    /// Delay between consecutive blinks (milliseconds)
    constexpr int BLINK_DELAY_MS = 200;

    /// Number of blinks for TX event
    constexpr int TX_BLINKS = 2;

    /// Number of blinks for RX event
    constexpr int RX_BLINKS = 1;
}

#endif // FIRMWARE_CONFIG_H
