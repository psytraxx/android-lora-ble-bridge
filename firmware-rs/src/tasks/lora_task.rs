//! ESP32-specific LoRa task.
//!
//! This task handles the LoRa radio hardware (SX1262) and communicates
//! with the MessageRouter via channels.

use crate::constants::*;
use crate::drivers::sx1262_direct;
use crate::protocol::Message;
use embassy_embedded_hal::shared_bus::asynch::spi::SpiDevice;
use embassy_futures::select::{Either, select};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::{Receiver, Sender};
use embassy_sync::mutex::Mutex;
use esp_hal::{
    Async,
    gpio::{AnyPin, Input, InputConfig, Output, OutputConfig},
    time::Rate,
};
use log::{debug, error, info, warn};
use lora_phy::iv::GenericSx126xInterfaceVariant;
use lora_phy::mod_params::*;
use lora_phy::{
    LoRa, RxMode,
    sx126x::{Config as Sx126xConfig, Sx126x, Sx1262, TcxoCtrlVoltage},
};
use static_cell::StaticCell;

/// Received Signal Strength Indicator in dBm
/// Valid range: approximately -140 dBm (weak) to -20 dBm (strong)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RssiDbm(i16);

impl RssiDbm {
    pub const fn new(value: i16) -> Self {
        Self(value)
    }

    pub const fn as_i16(self) -> i16 {
        self.0
    }
}

impl core::fmt::Display for RssiDbm {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "{} dBm", self.0)
    }
}

/// Signal-to-Noise Ratio in dB
/// Positive values indicate good signal, negative values indicate noise dominance
/// Typical range: -20 dB (poor) to +10 dB (excellent)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SnrDb(i8);

impl SnrDb {
    pub const fn new(value: i8) -> Self {
        Self(value)
    }

    pub const fn as_i8(self) -> i8 {
        self.0
    }
}

impl core::fmt::Display for SnrDb {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "{} dB", self.0)
    }
}

/// Metadata for received LoRa packets
#[derive(Debug, Clone, Copy)]
pub struct RadioMetadata {
    pub rssi: RssiDbm,
    pub snr: SnrDb,
}

/// LoRa GPIO pins configuration for SX126x
pub struct LoraGpios<'a> {
    pub cs: AnyPin<'a>,
    pub reset: AnyPin<'a>,
    pub dio1: AnyPin<'a>,
    pub busy: AnyPin<'a>,
    pub sck: AnyPin<'a>,
    pub miso: AnyPin<'a>,
    pub mosi: AnyPin<'a>,
}

static SPI_BUS: StaticCell<
    Mutex<CriticalSectionRawMutex, esp_hal::spi::master::Spi<'static, Async>>,
> = StaticCell::new();

#[embassy_executor::task]
pub async fn esp32_lora_task(
    spi_peripheral: esp_hal::peripherals::SPI2<'static>,
    gpios: LoraGpios<'static>,
    tx_to_lora: Receiver<'static, CriticalSectionRawMutex, Message, 5>,
    rx_from_lora: Sender<'static, CriticalSectionRawMutex, (Message, RadioMetadata), 3>,
    is_wakeup: bool,
) {
    info!(
        "[LoRa] Starting LoRa task ({} start)...",
        if is_wakeup { "warm" } else { "cold" }
    );
    info!(
        "[LoRa] Configuration: freq={} Hz, SF={}, BW={} kHz, CR=4/{}, preamble={} symbols",
        LORA_FREQUENCY_HZ,
        LORA_SPREADING_FACTOR,
        LORA_BANDWIDTH_HZ / 1000,
        LORA_CODING_RATE,
        LORA_PREAMBLE_LENGTH
    );

    // Initialize SPI
    info!("[LoRa] Initializing SPI bus at 1 MHz...");
    let spi = esp_hal::spi::master::Spi::new(
        spi_peripheral,
        esp_hal::spi::master::Config::default().with_frequency(Rate::from_mhz(1)),
    )
    .unwrap()
    .with_sck(gpios.sck)
    .with_mosi(gpios.mosi)
    .with_miso(gpios.miso)
    .into_async();

    let spi_bus = SPI_BUS.init(Mutex::new(spi));
    info!("[LoRa] SPI bus initialized");

    // Initialize LoRa radio GPIO pins
    // Note: cs and busy are mutable for potential wake packet reading
    let mut cs = Output::new(
        gpios.cs,
        esp_hal::gpio::Level::High,
        OutputConfig::default(),
    );
    let reset = Output::new(
        gpios.reset,
        esp_hal::gpio::Level::High,
        OutputConfig::default(),
    );
    let dio1 = Input::new(gpios.dio1, InputConfig::default());
    let mut busy = Input::new(gpios.busy, InputConfig::default());

    // If waking from deep sleep, read the buffered packet BEFORE lora-phy init.
    // The SX1262 has a packet in its FIFO that triggered the DIO1 wake interrupt.
    // lora-phy's rx().await would start a NEW RX session, missing this packet.
    if is_wakeup {
        info!("[LoRa] Deep sleep wake - reading buffered packet...");
        let mut wake_buffer = [0u8; 64];

        match sx1262_direct::read_wake_packet(spi_bus, &mut cs, &mut busy, &mut wake_buffer).await {
            Ok(Some((len, rssi, snr))) => {
                info!(
                    "[LoRa] Wake packet: {} bytes (RSSI: {} dBm, SNR: {} dB)",
                    len, rssi, snr
                );

                match Message::deserialize(&wake_buffer[..len as usize]) {
                    Ok(msg) => {
                        info!("[LoRa] Wake packet decoded: {:?}", msg);
                        let metadata = RadioMetadata {
                            rssi: RssiDbm::new(rssi),
                            snr: SnrDb::new(snr),
                        };
                        match rx_from_lora.try_send((msg, metadata)) {
                            Ok(_) => info!("[LoRa] Wake packet sent to router"),
                            Err(_) => warn!("[LoRa] Wake packet: router channel full, dropped!"),
                        }
                    }
                    Err(e) => {
                        warn!("[LoRa] Wake packet deserialize failed: {}", e);
                        debug!(
                            "[LoRa] Wake packet raw: {:02X?}",
                            &wake_buffer[..len as usize]
                        );
                    }
                }
            }
            Ok(None) => {
                info!("[LoRa] Wake: no buffered packet found (false wake or already processed)");
            }
            Err(e) => {
                warn!("[LoRa] Wake packet read error: {:?}", e);
            }
        }
    }

    let iv = GenericSx126xInterfaceVariant::new(reset, dio1, busy, None, None).unwrap();

    let chip_config = Sx126xConfig {
        chip: Sx1262,
        tcxo_ctrl: Some(TcxoCtrlVoltage::Ctrl1V8),
        use_dcdc: true,
        rx_boost: true,
    };
    let spi_device = SpiDevice::new(spi_bus, cs);
    let radio_hw = Sx126x::new(spi_device, iv, chip_config);

    let mut lora = LoRa::new(radio_hw, false, embassy_time::Delay)
        .await
        .expect("Failed to initialize LoRa radio");
    info!("[LoRa] Radio initialized");

    info!("[LoRa] Configuring modulation parameters...");
    let modulation_params = lora
        .create_modulation_params(
            SpreadingFactor::_11,
            Bandwidth::_250KHz,
            CodingRate::_4_5,
            LORA_FREQUENCY_HZ,
        )
        .unwrap();

    let mut tx_packet_params = lora
        .create_tx_packet_params(
            LORA_PREAMBLE_LENGTH as u16,
            false,
            true,
            false,
            &modulation_params,
        )
        .unwrap();

    let rx_packet_params = lora
        .create_rx_packet_params(
            LORA_PREAMBLE_LENGTH as u16,
            false,
            255,
            true,
            false,
            &modulation_params,
        )
        .unwrap();

    let rx_duty_cycle = RxMode::DutyCycle(DutyCycleParams {
        rx_time: DUTY_CYCLE_RX_PERIOD,
        sleep_time: DUTY_CYCLE_SLEEP_PERIOD,
    });

    info!(
        "[LoRa] Entering RX duty-cycle mode (RX: {} us, sleep: {} us)...",
        DUTY_CYCLE_RX_PERIOD * 1000 / 64,
        DUTY_CYCLE_SLEEP_PERIOD * 1000 / 64
    );

    match lora
        .prepare_for_rx(rx_duty_cycle, &modulation_params, &rx_packet_params)
        .await
    {
        Ok(_) => info!("[LoRa] Ready - listening for packets"),
        Err(e) => {
            error!("[LoRa] FATAL: Failed to enter RX mode: {:?}", e);
            panic!("LoRa failed to enter RX mode");
        }
    }

    let mut rx_buffer = [0u8; 64];
    let mut tx_count: u32 = 0;
    let mut rx_count: u32 = 0;

    loop {
        // Concurrently wait for TX requests or RX packets
        match select(
            tx_to_lora.receive(),
            lora.rx(&rx_packet_params, &mut rx_buffer),
        )
        .await
        {
            Either::First(msg) => {
                // Handle TX
                tx_count += 1;
                info!("[LoRa] TX #{}: Preparing to send {:?}", tx_count, msg);

                let mut buf = [0u8; 64];
                match msg.serialize(&mut buf) {
                    Ok(len) => {
                        info!(
                            "[LoRa] TX #{}: Serialized {} bytes, transmitting at {} dBm...",
                            tx_count, len, LORA_TX_POWER_DBM
                        );
                        match lora
                            .prepare_for_tx(
                                &modulation_params,
                                &mut tx_packet_params,
                                LORA_TX_POWER_DBM,
                                &buf[..len],
                            )
                            .await
                        {
                            Ok(()) => match lora.tx().await {
                                Ok(()) => {
                                    info!("[LoRa] TX #{}: Transmission complete", tx_count);
                                }
                                Err(e) => {
                                    error!("[LoRa] TX #{}: Transmission FAILED: {:?}", tx_count, e);
                                }
                            },
                            Err(e) => {
                                error!("[LoRa] TX #{}: prepare_for_tx FAILED: {:?}", tx_count, e);
                            }
                        }
                        // Return to RX duty-cycle mode
                        debug!("[LoRa] Returning to RX duty-cycle mode...");
                        if let Err(e) = lora
                            .prepare_for_rx(rx_duty_cycle, &modulation_params, &rx_packet_params)
                            .await
                        {
                            error!("[LoRa] Failed to return to RX mode: {:?}", e);
                        }
                    }
                    Err(e) => {
                        error!(
                            "[LoRa] TX #{}: Failed to serialize message: {}",
                            tx_count, e
                        );
                    }
                }
            }
            Either::Second(Ok((len, status))) => {
                // Handle RX
                rx_count += 1;
                info!(
                    "[LoRa] RX #{}: Received {} bytes (RSSI: {} dBm, SNR: {} dB)",
                    rx_count, len, status.rssi, status.snr
                );

                match Message::deserialize(&rx_buffer[..len as usize]) {
                    Ok(msg) => {
                        info!("[LoRa] RX #{}: Decoded message: {:?}", rx_count, msg);
                        let metadata = RadioMetadata {
                            rssi: RssiDbm::new(status.rssi),
                            snr: SnrDb::new(status.snr as i8),
                        };
                        match rx_from_lora.try_send((msg, metadata)) {
                            Ok(_) => {
                                debug!("[LoRa] RX #{}: Message sent to router", rx_count);
                            }
                            Err(_) => {
                                error!(
                                    "[LoRa] RX #{}: Router channel FULL, message DROPPED!",
                                    rx_count
                                );
                            }
                        }
                    }
                    Err(e) => {
                        error!(
                            "[LoRa] RX #{}: Failed to deserialize ({} bytes): {}",
                            rx_count, len, e
                        );
                        debug!(
                            "[LoRa] RX #{}: Raw data: {:02X?}",
                            rx_count,
                            &rx_buffer[..len as usize]
                        );
                    }
                }
            }
            Either::Second(Err(e)) => {
                warn!("[LoRa] RX error (radio will recover): {:?}", e);
                // In duty-cycle mode, errors put the radio in standby.
                // Re-prepare so the next rx() call works.
                if let Err(e) = lora
                    .prepare_for_rx(rx_duty_cycle, &modulation_params, &rx_packet_params)
                    .await
                {
                    error!("[LoRa] Failed to recover RX mode after error: {:?}", e);
                }
            }
        }
    }
}
