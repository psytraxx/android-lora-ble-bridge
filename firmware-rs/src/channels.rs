//! Inter-task communication channels
//!
//! This module defines all channels used for communication between Embassy tasks.
//! Centralizing them here provides:
//! - Single source of truth for channel capacities
//! - Clear documentation of data flow
//! - Easier modification and maintenance

use crate::protocol::Message;
use crate::tasks::{LedCommand, RadioMetadata};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_sync::signal::Signal;
use embassy_time::Instant;

/// All inter-task communication channels
///
/// Channel topology:
/// ```text
///                     ┌──────────────┐
///                     │   Watchdog   │
///                     └──────┬───────┘
///                            │ disconn_cmd
///                            ▼
/// ┌─────────┐  ble_rx   ┌─────────────┐  lora_tx   ┌─────────┐
/// │   BLE   │◄─────────►│   Router    │◄──────────►│  LoRa   │
/// │  Task   │  ble_tx   │             │  lora_rx   │  Task   │
/// └────┬────┘           └──────┬──────┘            └─────────┘
///      │ conn_state            │ led_cmd
///      │                       ▼
///      │               ┌─────────────┐
///      │               │  LED Task   │
///      │               └─────────────┘
///      │ bat_level
///      ▼
/// ┌─────────┐
/// │ Battery │
/// │  Task   │
/// └─────────┘
/// ```
pub struct Channels {
    /// Router → BLE: Messages to send to connected client (capacity: 10)
    pub ble_tx: Channel<CriticalSectionRawMutex, Message, 10>,

    /// BLE → Router: Messages received from client (capacity: 5)
    pub ble_rx: Channel<CriticalSectionRawMutex, Message, 5>,

    /// Router → LoRa: Messages to transmit over radio (capacity: 5)
    pub lora_tx: Channel<CriticalSectionRawMutex, Message, 5>,

    /// LoRa → Router: Messages received from radio with metadata (capacity: 3)
    pub lora_rx: Channel<CriticalSectionRawMutex, (Message, RadioMetadata), 3>,

    /// Router → LED: Blink pattern commands (capacity: 5)
    pub led_cmd: Channel<CriticalSectionRawMutex, LedCommand, 5>,

    /// Battery → BLE: Battery level percentage updates (capacity: 1)
    pub bat_level: Channel<CriticalSectionRawMutex, u8, 1>,

    /// BLE → Router: Connection state changes (capacity: 1)
    pub conn_state: Channel<CriticalSectionRawMutex, bool, 1>,

    /// Watchdog → BLE: Disconnect command on inactivity timeout (capacity: 1)
    pub disconn_cmd: Channel<CriticalSectionRawMutex, (), 1>,

    /// Router → Watchdog: Activity signal (not a channel, instant delivery)
    pub activity: Signal<CriticalSectionRawMutex, Instant>,

    /// LoRa → BLE: Last received signal quality for Device Info characteristic (RSSI dBm, SNR dB)
    pub radio_stats: Signal<CriticalSectionRawMutex, (i16, i8)>,

    /// BLE → Router: sent when the client enables TX notifications (CCCD write 0x0001).
    /// Capacity 1 so try_receive() consumes it and stale values from a previous
    /// connection are cleared at the start of each routing_loop.
    pub cccd_ready: Channel<CriticalSectionRawMutex, (), 1>,
}

impl Channels {
    /// Create all channels with their configured capacities
    pub const fn new() -> Self {
        Self {
            ble_tx: Channel::new(),
            ble_rx: Channel::new(),
            lora_tx: Channel::new(),
            lora_rx: Channel::new(),
            led_cmd: Channel::new(),
            bat_level: Channel::new(),
            conn_state: Channel::new(),
            disconn_cmd: Channel::new(),
            activity: Signal::new(),
            radio_stats: Signal::new(),
            cccd_ready: Channel::new(),
        }
    }
}

impl Default for Channels {
    fn default() -> Self {
        Self::new()
    }
}
