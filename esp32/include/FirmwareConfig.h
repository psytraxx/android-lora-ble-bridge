#ifndef FIRMWARE_CONFIG_H
#define FIRMWARE_CONFIG_H

#include <cstdint>

/**
 * @file FirmwareConfig.h
 * @brief Centralized configuration for ESP32 LoRa-BLE Bridge firmware
 *
 * This header consolidates all firmware constants, timeouts, and magic numbers
 * in one location for easy modification and documentation. Replaces scattered
 * compile-time defines with structured configuration.
 *
 * Design Goals:
 *  - Single source of truth for configuration values
 *  - Self-documenting with clear comments
 *  - Type-safe constants instead of preprocessor macros
 *  - Easy to port to runtime configuration (JSON/EEPROM) later
 */

//==============================================================================
// GPIO Pin Configuration
//==============================================================================

/// GPIO pin configuration for peripherals
/// Note: These are still defined as build-time constants via platformio.ini
/// for hardware portability. Access via LORA_*, LED_PIN, WAKE_BUTTON macros.

// LoRa SPI pins - defined in platformio.ini:
// LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS, LORA_RST, LORA_DIO0

// LED status indicator - defined in platformio.ini:
// LED_PIN

// Wake button - defined in platformio.ini:
// WAKE_BUTTON

//==============================================================================
// LoRa Radio Configuration
//==============================================================================

namespace LoRaConstants
{
    /// Default LoRa sync word (0x12 = private network, 0x34 = public LoRaWAN)
    constexpr uint8_t SYNC_WORD = 0x12;

    /// Time to wait for radio hardware to settle after mode change (TX/RX switch)
    constexpr int RX_SETTLE_TIME_MS = 50;

    /// Delay before sending ACK to ensure sender has switched to RX mode
    /// Timing: TX complete + mode switch + settle time = ~200ms minimum
    /// 500ms provides safe margin
    constexpr int ACK_DELAY_MS = 500;

    /// Number of retry attempts for LoRa initialization
    constexpr int INIT_RETRY_COUNT = 3;

    /// Delay between LoRa initialization retries (milliseconds)
    constexpr int INIT_RETRY_DELAY_MS = 1000;
}

//==============================================================================
// BLE Configuration
//==============================================================================

// Note: DEVICE_NAME is defined in platformio.ini as a build flag:
//   -DDEVICE_NAME='"ESP32-LoRa"'     (for esp32dev)
//   -DDEVICE_NAME='"ESP32S3-LoRa"'   (for lilygo-t-display-s3)

namespace BLEConstants
{
    /// BLE service UUID (application-specific)
    constexpr const char *SERVICE_UUID = "00001234-0000-1000-8000-00805f9b34fb";

    /// BLE TX characteristic UUID (ESP32 -> Android notifications)
    constexpr const char *TX_CHARACTERISTIC_UUID = "00005678-0000-1000-8000-00805f9b34fb";

    /// BLE RX characteristic UUID (Android -> ESP32 writes)
    constexpr const char *RX_CHARACTERISTIC_UUID = "00005679-0000-1000-8000-00805f9b34fb";

    /// Minimum BLE advertising interval (in 0.625ms units)
    /// 1600 * 0.625ms = 1000ms (1 second)
    constexpr int ADV_MIN_INTERVAL = 1600;

    /// Maximum BLE advertising interval (in 0.625ms units)
    /// 3200 * 0.625ms = 2000ms (2 seconds)
    constexpr int ADV_MAX_INTERVAL = 3200;

    /// BLE TX power level for balance of range (~10m) vs power consumption
    /// Options: P9 (+9dBm max), P6 (+6dBm), P3 (+3dBm balanced), P0 (0dBm), N3 (-3dBm min)
    constexpr int TX_POWER_LEVEL = 3; // ESP_PWR_LVL_P3

    /// Number of retry attempts for BLE initialization
    constexpr int INIT_RETRY_COUNT = 3;

    /// Delay between BLE initialization retries (milliseconds)
    constexpr int INIT_RETRY_DELAY_MS = 1000;

    /// Time to wait after disconnect before restarting advertising
    /// Allows NimBLE stack to clean up connection state
    constexpr int DISCONNECT_SETTLE_MS = 300;

    /// Time to wait after new connection before sending buffered messages
    /// Allows Android to: request MTU, discover services, enable notifications
    constexpr int CONNECTION_SETUP_DELAY_MS = 1000;

    /// Spacing between consecutive BLE message sends to avoid overwhelming stack
    constexpr int MESSAGE_SPACING_MS = 20;
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

    /// Maximum LoRa payload size (RadioLib SX1278 limit)
    constexpr int MAX_LORA_PAYLOAD = 256;

    /// Maximum protocol message size (ACK=2, Text+GPS=52)
    constexpr int MAX_PROTOCOL_MESSAGE = 64;
}

//==============================================================================
// Power Management Configuration
//==============================================================================

namespace PowerConstants
{
    /// CPU frequency in MHz (set via platformio.ini CPU_FREQ_MHZ)
    /// ESP32: 80 MHz for power savings vs 240 MHz max
    /// ESP32-S3: 160 MHz for balance

    /// Minimum CPU frequency for dynamic frequency scaling
    constexpr int CPU_MIN_FREQ_MHZ = 20;

    /// BLE advertising duration before entering light sleep (milliseconds)
    /// 30 seconds provides good discoverability window while conserving power
    constexpr unsigned long ADVERTISE_DURATION_MS = 30000UL;

    /// BLE connection inactivity timeout before forced disconnect (milliseconds)
    /// 60 seconds allows for casual message reading without premature disconnection
    /// Note: Android app expects 30s timeout (see BleConstants.AUTO_DISCONNECT_DELAY_MS)
    constexpr unsigned long INACTIVITY_TIMEOUT_MS = 60000UL;
}

//==============================================================================
// Main Loop Timing Configuration
//==============================================================================

namespace LoopConstants
{
    /// Loop delay when activity detected (short for responsiveness)
    constexpr int ACTIVE_DELAY_MS = 10;

    /// Loop delay when idle (long to enable light sleep for power savings)
    /// 500ms provides good balance: responsive enough for messaging (~0.5s max latency)
    /// yet long enough to enter light sleep for power savings
    constexpr int IDLE_DELAY_MS = 500;
}

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
