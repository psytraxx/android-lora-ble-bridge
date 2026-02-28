//! Firmware Configuration Constants
//!
//! This module contains all configuration constants for the LoRa-BLE Bridge firmware.
//! These values match the Arduino firmware's FirmwareConfig.h to ensure protocol compatibility.
//!==============================================================================
//! LoRa Radio Configuration
//!==============================================================================

/// LoRa frequency in Hz (433.92 MHz)
pub const LORA_FREQUENCY_HZ: u32 = 433_920_000;

/// LoRa bandwidth in Hz (125 kHz) - matches C++ firmware
pub const LORA_BANDWIDTH_HZ: u32 = 125_000;

/// LoRa spreading factor (7-12) - SF11 matches Arduino firmware for interoperability
pub const LORA_SPREADING_FACTOR: u8 = 11;

/// LoRa coding rate (5 = 4/5, 6 = 4/6, 7 = 4/7, 8 = 4/8) - matches C++ CODING_RATE = 7
pub const LORA_CODING_RATE: u8 = 7;

/// LoRa sync word (0x12 = private network, 0x34 = public LoRaWAN)
pub const LORA_SYNC_WORD: u8 = 0x12;

/// LoRa preamble length (64 symbols for effective duty cycling, matches Arduino)
pub const LORA_PREAMBLE_LENGTH: u8 = 64;

/// LoRa TX power in dBm (matches Arduino platformio.ini: 22 dBm max legal power)
pub const LORA_TX_POWER_DBM: i32 = 22;

/// Time to wait for radio hardware to settle after mode change (TX/RX switch)
pub const LORA_RX_SETTLE_TIME_MS: u32 = 50;

/// Safety margin added to Time-on-Air calculations
pub const LORA_TIMING_MARGIN_MS: u32 = 500;

/// TCXO voltage for SX1262/SX1268 radios (1.8V)
pub const LORA_TCXO_VOLTAGE: f32 = 1.8;

/// RX duty-cycle parameters (matches RadioLib startReceiveDutyCycleAuto algorithm).
/// The SX1262 alternates between brief RX windows and sleep to save power.
/// Constraint: rx + sleep < preamble duration so a packet is always detected.
///
/// Symbol period = 2^SF / BW = 2^11/125kHz = 16.384ms
/// RX window = 9 symbols = 147.5ms (8 min for detection + 1 margin)
/// Sleep window = 48 symbols = 786ms (preamble(64) - 2*8 detection margin)
/// Total cycle = 933.5ms < 1048.6ms preamble → always catches ≥7 preamble symbols
///
/// Values are SX1262 raw timer steps (15.625µs each).
const SYMBOL_PERIOD_US: u32 =
    ((1u32 << LORA_SPREADING_FACTOR as u32) * 1_000_000) / LORA_BANDWIDTH_HZ;
const DUTY_CYCLE_MIN_SYMBOLS: u32 = 8;
pub const DUTY_CYCLE_RX_PERIOD: u32 = (DUTY_CYCLE_MIN_SYMBOLS + 1) * SYMBOL_PERIOD_US * 64 / 1000;
pub const DUTY_CYCLE_SLEEP_PERIOD: u32 =
    (LORA_PREAMBLE_LENGTH as u32 - 2 * DUTY_CYCLE_MIN_SYMBOLS) * SYMBOL_PERIOD_US * 64 / 1000;

//==============================================================================
// BLE Configuration (matches Arduino BLEConstants)
//==============================================================================

/// BLE device name base (MAC suffix will be appended: "HeltecLite-LoRa-XXXX")
pub const BLE_DEVICE_NAME_BASE: &str = "HeltecLite-LoRa";

/// BLE advertising minimum interval in milliseconds (matches Arduino: 1600 units = 1000ms)
pub const BLE_ADV_INTERVAL_MIN_MS: u64 = 1000;

/// BLE advertising maximum interval in milliseconds (matches Arduino: 3200 units = 2000ms)
pub const BLE_ADV_INTERVAL_MAX_MS: u64 = 2000;

/// BLE service UUID (matches Android app and Arduino firmware)
pub const BLE_SERVICE_UUID: &str = "00001234-0000-1000-8000-00805f9b34fb";

/// BLE TX characteristic UUID (Device -> Android notifications)
pub const BLE_TX_CHARACTERISTIC_UUID: &str = "00005678-0000-1000-8000-00805f9b34fb";

/// BLE RX characteristic UUID (Android -> Device writes)
pub const BLE_RX_CHARACTERISTIC_UUID: &str = "00005679-0000-1000-8000-00805f9b34fb";

/// Standard BLE Battery Service UUID
pub const BLE_BATTERY_SERVICE_UUID: &str = "0000180f-0000-1000-8000-00805f9b34fb";

/// Standard BLE Battery Level Characteristic UUID
pub const BLE_BATTERY_LEVEL_UUID: &str = "00002a19-0000-1000-8000-00805f9b34fb";

/// Device Info characteristic UUID (read + notify, 16 bytes)
/// Format: [Battery:1][RSSI:2LE][SNR×100:2LE][TxPower:1][Freq:4LE][BW:4LE][SF:1][CR:1]
pub const BLE_INFO_CHARACTERISTIC_UUID: &str = "0000567a-0000-1000-8000-00805f9b34fb";

//==============================================================================
// Power Management Configuration
//==============================================================================

/// Inactivity timeout before entering deep sleep (milliseconds)
/// Matches Arduino FirmwareConfig.h (60 seconds)
pub const INACTIVITY_TIMEOUT_MS: u64 = 60_000;

/// Advertising duration when disconnected (seconds)
/// Matches Arduino power management behavior
pub const ADVERTISING_DURATION_SECS: u64 = 60;

//==============================================================================
// Battery Monitoring Configuration
//==============================================================================

/// OCV (Open Circuit Voltage) lookup table for Li-ion battery (millivolts)
/// Index 0 = 100% charged, Index 10 = 0% charged
/// Matches Arduino FirmwareConfig.h OCV table
pub const OCV_TABLE: [u16; 11] = [
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
    3100, // 0% - Cut-off voltage
];

//==============================================================================
// Watchdog Timer Configuration
//==============================================================================

/// Watchdog timeout in seconds
/// LoRa TX at SF11+BW31kHz can take 2-3s, so 10s provides safe margin
/// Matches Arduino FirmwareConfig.h
pub const WATCHDOG_TIMEOUT_SECS: u64 = 10;

//==============================================================================
// LED Configuration (matches Arduino LEDConstants)
//==============================================================================

/// LED on duration (milliseconds) - matches Arduino
pub const LED_ON_MS: u64 = 50;

/// Delay between consecutive blinks (milliseconds) - matches Arduino
pub const LED_BLINK_DELAY_MS: u64 = 200;

/// Number of blinks for TX event
pub const LED_TX_BLINKS: u8 = 2;

/// Number of blinks for RX event
pub const LED_RX_BLINKS: u8 = 1;

/// Heartbeat LED interval (milliseconds) - matches Arduino 2 second heartbeat
pub const LED_HEARTBEAT_INTERVAL_MS: u64 = 2000;

/// Heartbeat LED on duration (milliseconds) - barely visible 5ms pulse to save power
pub const LED_HEARTBEAT_ON_MS: u64 = 5;

//==============================================================================
// CAD (Channel Activity Detection) Configuration
//==============================================================================

/// Max CAD attempts before force-transmitting (matches C++ CAD_MAX_RETRIES)
pub const CAD_MAX_RETRIES: u8 = 5;

/// Base backoff delay between CAD retries (milliseconds)
pub const CAD_BACKOFF_BASE_MS: u64 = 50;

/// Maximum random jitter added to CAD backoff (milliseconds)
pub const CAD_BACKOFF_JITTER_MS: u64 = 100;

//==============================================================================
// Message Retry Configuration
//==============================================================================

/// Number of retransmit attempts for user text messages (BLE → LoRa)
/// Matches C++ retry behavior: user msgs 3x, ACK/broadcast 0x
pub const LORA_TEXT_RETRIES: u8 = 3;

/// Timeout before retrying a text message (milliseconds)
/// Must exceed roundtrip time (our TX + remote ACK TX) at SF11/BW125
/// SF11/BW125 ~40-byte packet ToA ≈ 2s, so 8s gives comfortable margin
pub const LORA_RETRY_TIMEOUT_MS: u64 = 8_000;

//==============================================================================
// Buffer Configuration
//==============================================================================

/// Maximum messages to buffer when BLE disconnected
pub const MAX_BUFFERED_MESSAGES: usize = 10;

/// Delay after BLE connection before draining buffered messages (milliseconds)
/// This gives the client time to enable notifications (write to CCCD).
/// Arduino firmware achieves this by polling in main loop; we use explicit delay.
pub const BUFFER_DRAIN_DELAY_MS: u64 = 500;

/// Maximum LoRa payload size
pub const MAX_LORA_PAYLOAD: usize = 256;

/// Maximum protocol message size
pub const MAX_PROTOCOL_MESSAGE: usize = 64;

//==============================================================================
// GPIO Pin Configuration (Heltec WiFi LoRa V3 with ESP32-S3 + SX1262)
//==============================================================================
pub mod heltec_wifi_lora_v3 {
    /// LoRa SPI SCK pin
    pub const LORA_SCK: u8 = 9;

    /// LoRa SPI MISO pin
    pub const LORA_MISO: u8 = 11;

    /// LoRa SPI MOSI pin
    pub const LORA_MOSI: u8 = 10;

    /// LoRa SPI CS (chip select) pin
    pub const LORA_SS: u8 = 8;

    /// LoRa reset pin
    pub const LORA_RST: u8 = 12;

    /// LoRa DIO1 interrupt pin (SX1262 uses DIO1, not DIO0)
    pub const LORA_DIO1: u8 = 14;

    /// LoRa BUSY pin (for SX1262)
    pub const LORA_BUSY: u8 = 13;

    /// LED pin (active HIGH)
    pub const LED_PIN: u8 = 35;

    /// Wake button pin (active LOW with pull-up)
    pub const WAKE_BUTTON: u8 = 0;

    /// VEXT control pin (LOW disables external peripherals for power savings)
    pub const VEXT_PIN: u8 = 36;

    /// Battery voltage ADC pin
    pub const BATTERY_ADC_PIN: u8 = 1;

    /// Battery ADC control pin (enable voltage divider circuit)
    pub const BATTERY_ADC_CTRL: u8 = 37;

    /// Battery voltage divider ratio (Heltec WiFi LoRa V3 specific)
    pub const BATTERY_VOLTAGE_DIVIDER: f32 = 5.1205;
}
