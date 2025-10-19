use embassy_embedded_hal::shared_bus::asynch::spi::SpiDevice;
use embassy_futures::select::{Either, select};
use embassy_sync::{
    blocking_mutex::raw::CriticalSectionRawMutex,
    channel::{Receiver, Sender},
    mutex::Mutex,
};
use embassy_time::Delay;
use esp_hal::{
    Async,
    gpio::{AnyPin, Input, InputConfig, Output, OutputConfig},
    time::Rate,
};
use log::{error, info, warn};
use lora_phy::mod_params::*;
use lora_phy::{
    LoRa, RxMode,
    iv::GenericSx127xInterfaceVariant,
    sx127x::{Config, Sx127x, Sx1276},
};
use static_cell::StaticCell;

use crate::protocol::{AckMessage, Message};

/// LoRa GPIO pins configuration
pub struct LoraGpios<'a> {
    pub cs: AnyPin<'a>,
    pub reset: AnyPin<'a>,
    pub dio0: AnyPin<'a>,
    pub sck: AnyPin<'a>,
    pub miso: AnyPin<'a>,
    pub mosi: AnyPin<'a>,
}

#[embassy_executor::task]
/// LoRa task that manages LoRa radio operations, including transmission and reception.
/// Handles message forwarding between LoRa and BLE channels, and sends acknowledgments.
/// Uses non-blocking send to BLE channel (capacity: 10) to continue receiving even when BLE is disconnected.
pub async fn lora_task(
    spi_peripheral: esp_hal::peripherals::SPI2<'static>,
    gpios: LoraGpios<'static>,
    ble_to_lora: Receiver<'static, CriticalSectionRawMutex, Message, 5>,
    lora_to_ble: Sender<'static, CriticalSectionRawMutex, Message, 10>,
) {
    info!("LoRa task starting...");

    // Initialize SPI
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

    let cs = Output::new(
        gpios.cs,
        esp_hal::gpio::Level::High,
        OutputConfig::default(),
    );
    let spi_device = SpiDevice::new(spi_bus, cs);

    let config = Config {
        chip: Sx1276,
        tcxo_used: false,
        tx_boost: false,
        rx_boost: false,
    };

    let reset = Output::new(
        gpios.reset,
        esp_hal::gpio::Level::High,
        OutputConfig::default(),
    );
    let dio0 = Input::new(gpios.dio0, InputConfig::default());

    let iv = match GenericSx127xInterfaceVariant::new(reset, dio0, None, None) {
        Ok(i) => i,
        Err(e) => {
            error!("Failed to create LoRa interface: {:?}", e);
            return;
        }
    };

    let radio = Sx127x::new(spi_device, iv, config);
    let mut lora: LoraRadio = match LoRa::new(radio, true, Delay).await {
        Ok(l) => l,
        Err(e) => {
            error!("Failed to create LoRa radio: {:?}", e);
            return;
        }
    };

    // Initialize LoRa
    info!("Initializing LoRa radio");
    if let Err(e) = lora.init().await {
        error!("Failed to initialize LoRa radio: {:?}", e);
        return;
    }

    // Configure TX power from environment variable (set in .cargo/config.toml)
    // Default: 14 dBm (~25 mW) - check local regulations for 433 MHz ISM band
    // SX1276 supports -4 dBm to +20 dBm on PA_BOOST pin
    let output_power: i32 = if let Some(power_str) = option_env!("LORA_TX_POWER_DBM") {
        match power_str.parse::<i32>() {
            Ok(v) if (-4..=20).contains(&v) => {
                info!("Using TX power from config: {} dBm", v);
                v
            }
            Ok(v) => {
                warn!(
                    "TX power {} dBm out of range (-4 to 20), using default 14 dBm",
                    v
                );
                14
            }
            Err(_) => {
                warn!(
                    "Invalid TX power value '{}', using default 14 dBm",
                    power_str
                );
                14
            }
        }
    } else {
        info!("TX power not configured, using default 14 dBm");
        14
    };

    // Configure LoRa frequency from environment variable (set in .cargo/config.toml)
    // Default: 433.92 MHz - standard frequency for 433 MHz ISM band
    // Valid ISM bands: 433.05-434.79 MHz (worldwide), 863-870 MHz (EU), 902-928 MHz (US)
    let frequency: u32 = if let Some(freq_str) = option_env!("LORA_TX_FREQUENCY") {
        match freq_str.parse::<u32>() {
            Ok(v)
                if (433_050_000..=434_790_000).contains(&v)
                    || (863_000_000..=870_000_000).contains(&v)
                    || (902_000_000..=928_000_000).contains(&v) =>
            {
                info!(
                    "Using frequency from config: {} Hz ({:.2} MHz)",
                    v,
                    v as f32 / 1_000_000.0
                );
                v
            }
            Ok(v) => {
                warn!(
                    "Frequency {} Hz ({:.2} MHz) outside common ISM bands, using default 433.92 MHz",
                    v,
                    v as f32 / 1_000_000.0
                );
                433_920_000
            }
            Err(_) => {
                warn!(
                    "Invalid frequency value '{}', using default 433.92 MHz",
                    freq_str
                );
                433_920_000
            }
        }
    } else {
        info!("Frequency not configured, using default 433.92 MHz");
        433_920_000
    };

    // Create modulation parameters optimized for long-range communication
    // SF10 + BW125 provides excellent range (5-10 km) with reasonable data rate
    // SF7 at 868MHz: ~40ms ToA for 61 bytes
    // SF10 at 433.92MHz: ~700ms ToA for 61 bytes (max message size with 50 char text)
    let modulation_params = match lora.create_modulation_params(
        SpreadingFactor::_10, // Higher SF = longer range, slower speed
        Bandwidth::_125KHz,   // Narrower BW = better sensitivity, longer range
        CodingRate::_4_5,     // Good error correction
        frequency,            // Frequency from config (default: 433.92 MHz)
    ) {
        Ok(p) => p,
        Err(e) => {
            error!("Failed to create LoRa modulation parameters: {:?}", e);
            return;
        }
    };

    // Create TX packet parameters
    let mut tx_packet_params =
        match lora.create_tx_packet_params(8, false, true, false, &modulation_params) {
            Ok(p) => p,
            Err(e) => {
                error!("Failed to create LoRa TX packet parameters: {:?}", e);
                return;
            }
        };

    // Create RX packet parameters
    let rx_packet_params =
        match lora.create_rx_packet_params(8, false, 255, true, false, &modulation_params) {
            Ok(p) => p,
            Err(e) => {
                error!("Failed to create LoRa RX packet parameters: {:?}", e);
                return;
            }
        };

    // Prepare for continuous receive
    if let Err(e) = lora
        .prepare_for_rx(RxMode::Continuous, &modulation_params, &rx_packet_params)
        .await
    {
        error!("Failed to prepare LoRa for RX: {:?}", e);
        return;
    }
    info!("LoRa radio ready for RX/TX operations");

    // Buffer sized for max message: 11 bytes + 50 char text = 61 bytes
    // Using 64 bytes (power of 2) for alignment
    let mut rx_buffer = [0u8; 64];

    loop {
        let ble_recv = ble_to_lora.receive();
        let lora_recv = lora.rx(&rx_packet_params, &mut rx_buffer);

        match select(ble_recv, lora_recv).await {
            Either::First(msg) => {
                info!("Received message from BLE to transmit via LoRa: {:?}", msg);
                // Transmit message over LoRa
                let mut buf = [0u8; 64];
                match msg.serialize(&mut buf) {
                    Ok(len) => {
                        match lora
                            .prepare_for_tx(
                                &modulation_params,
                                &mut tx_packet_params,
                                output_power,
                                &buf[..len],
                            )
                            .await
                        {
                            Ok(_) => match lora.tx().await {
                                Ok(_) => {
                                    info!("LoRa TX successful");
                                    // Return to RX mode after transmission
                                    if let Err(e) = lora
                                        .prepare_for_rx(
                                            RxMode::Continuous,
                                            &modulation_params,
                                            &rx_packet_params,
                                        )
                                        .await
                                    {
                                        error!("Failed to return to RX mode after TX: {:?}", e);
                                    }
                                }
                                Err(e) => error!("LoRa TX failed: {:?}", e),
                            },
                            Err(e) => error!("LoRa prepare_for_tx failed: {:?}", e),
                        }
                    }
                    Err(e) => error!("Failed to serialize message for LoRa TX: {:?}", e),
                }
            }
            Either::Second(result) => {
                // Handle received LoRa packet
                match result {
                    Ok((len, status)) => {
                        info!("LoRa RX: received {} bytes, RSSI: {:?}", len, status.rssi);
                        let data = &rx_buffer[..len as usize];
                        match Message::deserialize(data) {
                            Ok(msg) => {
                                info!("LoRa message deserialized: {:?}", msg);
                                match msg {
                                    Message::Text(ref text_msg) => {
                                        // Send ACK
                                        let ack = Message::Ack(AckMessage { seq: text_msg.seq });
                                        info!("Sending ACK for seq: {}", text_msg.seq);
                                        let mut buf = [0u8; 64];
                                        if let Ok(ack_len) = ack.serialize(&mut buf) {
                                            if let Err(e) = lora
                                                .prepare_for_tx(
                                                    &modulation_params,
                                                    &mut tx_packet_params,
                                                    output_power,
                                                    &buf[..ack_len],
                                                )
                                                .await
                                            {
                                                error!("Failed to prepare ACK TX: {:?}", e);
                                            } else if let Err(e) = lora.tx().await {
                                                error!("Failed to send ACK: {:?}", e);
                                            } else {
                                                info!("ACK sent successfully");
                                                // Return to RX mode after ACK transmission
                                                if let Err(e) = lora
                                                    .prepare_for_rx(
                                                        RxMode::Continuous,
                                                        &modulation_params,
                                                        &rx_packet_params,
                                                    )
                                                    .await
                                                {
                                                    error!(
                                                        "Failed to return to RX mode after ACK TX: {:?}",
                                                        e
                                                    );
                                                }
                                            }
                                        }
                                        // Forward data to BLE (non-blocking)
                                        // If channel is full (10 messages buffered), oldest will be dropped
                                        match lora_to_ble.try_send(msg) {
                                            Ok(_) => {
                                                info!("Text message forwarded from LoRa to BLE")
                                            }
                                            Err(_) => {
                                                warn!(
                                                    "BLE message buffer full (10 messages) - message dropped. Reconnect phone to receive buffered messages."
                                                );
                                            }
                                        }
                                    }
                                    Message::Gps(ref gps_msg) => {
                                        // Send ACK
                                        let ack = Message::Ack(AckMessage { seq: gps_msg.seq });
                                        info!("Sending ACK for GPS seq: {}", gps_msg.seq);
                                        let mut buf = [0u8; 64];
                                        if let Ok(ack_len) = ack.serialize(&mut buf) {
                                            if let Err(e) = lora
                                                .prepare_for_tx(
                                                    &modulation_params,
                                                    &mut tx_packet_params,
                                                    output_power,
                                                    &buf[..ack_len],
                                                )
                                                .await
                                            {
                                                error!("Failed to prepare ACK TX: {:?}", e);
                                            } else if let Err(e) = lora.tx().await {
                                                error!("Failed to send ACK: {:?}", e);
                                            } else {
                                                info!("ACK sent successfully");
                                                // Return to RX mode after ACK transmission
                                                if let Err(e) = lora
                                                    .prepare_for_rx(
                                                        RxMode::Continuous,
                                                        &modulation_params,
                                                        &rx_packet_params,
                                                    )
                                                    .await
                                                {
                                                    error!(
                                                        "Failed to return to RX mode after ACK TX: {:?}",
                                                        e
                                                    );
                                                }
                                            }
                                        }
                                        // Forward GPS data to BLE
                                        match lora_to_ble.try_send(msg) {
                                            Ok(_) => {
                                                info!("GPS message forwarded from LoRa to BLE")
                                            }
                                            Err(_) => {
                                                warn!(
                                                    "BLE message buffer full - GPS message dropped"
                                                );
                                            }
                                        }
                                    }
                                    Message::Ack(ref ack) => {
                                        info!("Received ACK for seq: {}", ack.seq);
                                        // Forward ACK to BLE (non-blocking)
                                        match lora_to_ble.try_send(msg) {
                                            Ok(_) => info!("ACK forwarded to BLE"),
                                            Err(_) => {
                                                warn!("BLE ACK buffer full - ACK dropped");
                                            }
                                        }
                                    }
                                }
                            }
                            Err(e) => warn!("Failed to deserialize LoRa message: {:?}", e),
                        }
                    }
                    Err(e) => warn!("LoRa RX error: {:?}", e),
                }
            }
        }
    }
}

pub type LoraRadio = LoRa<
    Sx127x<
        SpiDevice<
            'static,
            CriticalSectionRawMutex,
            esp_hal::spi::master::Spi<'static, Async>,
            Output<'static>,
        >,
        GenericSx127xInterfaceVariant<Output<'static>, Input<'static>>,
        Sx1276,
    >,
    Delay,
>;

static SPI_BUS: StaticCell<
    Mutex<CriticalSectionRawMutex, esp_hal::spi::master::Spi<'static, Async>>,
> = StaticCell::new();
