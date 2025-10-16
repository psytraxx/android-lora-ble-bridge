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
    gpio::{Input, InputConfig, Output, OutputConfig},
    time::Rate,
};
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
    pub cs: esp_hal::peripherals::GPIO5<'a>,
    pub reset: esp_hal::peripherals::GPIO12<'a>,
    pub dio0: esp_hal::peripherals::GPIO15<'a>,
    pub sck: esp_hal::peripherals::GPIO18<'a>,
    pub miso: esp_hal::peripherals::GPIO19<'a>,
    pub mosi: esp_hal::peripherals::GPIO21<'a>,
}

#[embassy_executor::task]
/// LoRa task that manages LoRa radio operations, including transmission and reception.
/// Handles message forwarding between LoRa and BLE channels, and sends acknowledgments.
pub async fn lora_task(
    spi_peripheral: esp_hal::peripherals::SPI2<'static>,
    gpios: LoraGpios<'static>,
    ble_to_lora: Receiver<'static, CriticalSectionRawMutex, Message, 1>,
    lora_to_ble: Sender<'static, CriticalSectionRawMutex, Message, 1>,
) {
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

    let iv = GenericSx127xInterfaceVariant::new(reset, dio0, None, None).unwrap();

    let radio = Sx127x::new(spi_device, iv, config);
    let mut lora: LoraRadio = LoRa::new(radio, true, Delay).await.unwrap();
    // Initialize LoRa
    lora.init().await.unwrap();

    // Create modulation parameters (adjust frequency as needed, e.g., 868MHz for EU)
    let modulation_params = lora
        .create_modulation_params(
            SpreadingFactor::_7,
            Bandwidth::_250KHz,
            CodingRate::_4_5,
            868_000_000,
        )
        .unwrap();

    // Create TX packet parameters
    let mut tx_packet_params = lora
        .create_tx_packet_params(8, false, true, false, &modulation_params)
        .unwrap();

    // Create RX packet parameters
    let rx_packet_params = lora
        .create_rx_packet_params(8, false, 255, true, false, &modulation_params)
        .unwrap();

    // Prepare for continuous receive
    lora.prepare_for_rx(RxMode::Continuous, &modulation_params, &rx_packet_params)
        .await
        .unwrap();

    let mut rx_buffer = [0u8; 256];

    loop {
        let ble_recv = ble_to_lora.receive();
        let lora_recv = lora.rx(&rx_packet_params, &mut rx_buffer);

        match select(ble_recv, lora_recv).await {
            Either::First(msg) => {
                // Transmit message over LoRa
                let mut buf = [0u8; 512];
                if let Ok(len) = msg.serialize(&mut buf) {
                    lora.prepare_for_tx(&modulation_params, &mut tx_packet_params, 14, &buf[..len])
                        .await
                        .unwrap();
                    lora.tx().await.unwrap();
                }
            }
            Either::Second(result) => {
                // Handle received LoRa packet
                if let Ok((len, _status)) = result {
                    let data = &rx_buffer[..len as usize];
                    if let Ok(msg) = Message::deserialize(data) {
                        match msg {
                            Message::Data(ref data) => {
                                // Send ACK
                                let ack = Message::Ack(AckMessage { seq: data.seq });
                                let mut buf = [0u8; 512];
                                if let Ok(ack_len) = ack.serialize(&mut buf) {
                                    lora.prepare_for_tx(
                                        &modulation_params,
                                        &mut tx_packet_params,
                                        14,
                                        &buf[..ack_len],
                                    )
                                    .await
                                    .unwrap();
                                    lora.tx().await.unwrap();
                                }
                                // Forward data to BLE
                                let _ = lora_to_ble.send(msg).await;
                            }
                            Message::Ack(_) => {
                                // Forward ACK to BLE
                                let _ = lora_to_ble.send(msg).await;
                            }
                        }
                    }
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
