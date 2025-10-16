//! ESP32-S3 Firmware for LoRa-BLE Bridge
//!
//! This firmware implements a BLE peripheral that communicates with Android devices
//! and bridges BLE messages to LoRa transmission and reception.
//!
//! Features:
//! - BLE GATT server with TX/RX characteristics for message exchange
//! - Channels for inter-task communication
//! - Advertising as "ESP32S3-LoRa"

#![no_std]
#![no_main]
#![deny(
    clippy::mem_forget,
    reason = "mem::forget is generally not safe to do with esp_hal types, especially those \
    holding buffers for the duration of a data transfer."
)]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}

use bt_hci::controller::ExternalController;
use embassy_embedded_hal::shared_bus::asynch::spi::SpiDevice;
use embassy_executor::Spawner;
use embassy_futures::join::join;
use embassy_futures::select::{Either, select};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::{Channel, Receiver, Sender};
use embassy_sync::mutex::Mutex;
use embassy_time::Delay;
use embassy_time::{Duration, Timer};
use esp_hal::Async;
use esp_hal::clock::CpuClock;
use esp_hal::gpio::Input;
use esp_hal::gpio::InputConfig;
use esp_hal::gpio::Output;
use esp_hal::gpio::OutputConfig;
use esp_hal::time::Rate;
use esp_hal::timer::timg::TimerGroup;
use esp_radio::Controller;
use esp_radio::ble::controller::BleConnector;
use esp32s3::protocol::{AckMessage, Message};
use lora_phy::iv::GenericSx127xInterfaceVariant;
use lora_phy::mod_params::*;
use static_cell::StaticCell;
use trouble_host::prelude::*;

use lora_phy::{LoRa, sx127x::*};

extern crate alloc;

const CONNECTIONS_MAX: usize = 1;
const L2CAP_CHANNELS_MAX: usize = 1;

// This creates a default app-descriptor required by the esp-idf bootloader.
// For more information see: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/app_image_format.html#application-description>
esp_bootloader_esp_idf::esp_app_desc!();

// GATT Server definition
/// GATT server with a custom LoRa service for message exchange.
/// The service has two characteristics: TX for outgoing messages and RX for incoming messages.
#[gatt_server]
struct Server {
    lora_service: LoraService,
}

/// Custom LoRa service with UUID 0x1234.
/// Provides characteristics for transmitting and receiving messages via BLE.
#[gatt_service(uuid = "1234")]
struct LoraService {
    /// TX characteristic (UUID 0x5678): Used to notify connected centrals of outgoing messages.
    /// Readable, writable, and notifiable.
    #[characteristic(uuid = "5678", read, write, notify, value = [0u8; 512])]
    tx: [u8; 512],
    /// RX characteristic (UUID 0x5679): Used to receive incoming messages from connected centrals.
    /// Readable, writable, and notifiable.
    #[characteristic(uuid = "5679", read, write, notify, value = [0u8; 512])]
    rx: [u8; 512],
}

#[esp_rtos::main]
async fn main(spawner: Spawner) -> ! {
    // Initialize ESP32-S3 peripherals and clock
    let config = esp_hal::Config::default().with_cpu_clock(CpuClock::max());
    let peripherals = esp_hal::init(config);

    // Allocate heap memory for dynamic allocations
    esp_alloc::heap_allocator!(#[unsafe(link_section = ".dram2_uninit")] size: 73744);

    // Initialize the RTOS timer
    let timg0 = TimerGroup::new(peripherals.TIMG0);
    esp_rtos::start(timg0.timer0);

    // Initialize Wi-Fi/BLE radio
    let radio_init = esp_radio::init().expect("Failed to initialize Wi-Fi/BLE controller");
    let radio = RADIO.init(radio_init);
    // Create BLE connector and controller
    let transport = BleConnector::new(radio, peripherals.BT, Default::default()).unwrap();
    let ble_controller = ExternalController::<_, 20>::new(transport);

    // Initialize communication channels between BLE and LoRa tasks
    let ble_to_lora = BLE_TO_LORA.init(Channel::new());
    let lora_to_ble = LORA_TO_BLE.init(Channel::new());

    // Spawn the BLE task to handle BLE communication
    spawner
        .spawn(ble_task(
            ble_controller,
            ble_to_lora.sender(),
            lora_to_ble.receiver(),
        ))
        .unwrap();

    // LoRa setup
    let spi = esp_hal::spi::master::Spi::new(
        peripherals.SPI2,
        esp_hal::spi::master::Config::default().with_frequency(Rate::from_mhz(1)),
    )
    .unwrap()
    .with_sck(peripherals.GPIO18)
    .with_mosi(peripherals.GPIO21)
    .with_miso(peripherals.GPIO19)
    .into_async();

    let spi_bus = SPI_BUS.init(Mutex::new(spi));

    let cs = Output::new(
        peripherals.GPIO5,
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
        peripherals.GPIO12,
        esp_hal::gpio::Level::High,
        OutputConfig::default(),
    );
    let dio0 = Input::new(peripherals.GPIO15, InputConfig::default());

    let iv = GenericSx127xInterfaceVariant::new(reset, dio0, None, None).unwrap();

    let radio = Sx127x::new(spi_device, iv, config);
    let lora: LoraRadio = LoRa::new(radio, true, Delay).await.unwrap();

    // Spawn LoRa task
    spawner
        .spawn(lora_task(
            lora,
            ble_to_lora.receiver(),
            lora_to_ble.sender(),
        ))
        .unwrap();

    // Main loop: keep the system running
    loop {
        Timer::after(Duration::from_secs(1)).await;
    }
}

#[embassy_executor::task]
/// BLE task that handles BLE stack initialization, advertising, and GATT event processing.
/// Forwards messages between BLE and LoRa channels.
async fn ble_task(
    controller: ExternalController<BleConnector<'static>, 20>,
    mut ble_to_lora: Sender<'static, CriticalSectionRawMutex, Message, 1>,
    mut lora_to_ble: Receiver<'static, CriticalSectionRawMutex, Message, 1>,
) {
    // Set a random address for the BLE device
    let address: Address = Address::random([0xff, 0x8f, 0x1a, 0x05, 0xe4, 0xff]);
    // Initialize host resources for BLE stack
    let mut resources: HostResources<DefaultPacketPool, CONNECTIONS_MAX, L2CAP_CHANNELS_MAX> =
        HostResources::new();
    let stack = trouble_host::new(controller, &mut resources).set_random_address(address);
    let Host {
        mut peripheral,
        runner,
        ..
    } = stack.build();

    // Create the GATT server with peripheral configuration
    let server = Server::new_with_config(GapConfig::Peripheral(PeripheralConfig {
        name: "ESP32S3-LoRa",
        appearance: &appearance::power_device::GENERIC_POWER_DEVICE,
    }))
    .unwrap();

    // Prepare advertising data
    let mut adv_data = [0; 31];
    let adv_data_len = AdStructure::encode_slice(
        &[
            AdStructure::Flags(LE_GENERAL_DISCOVERABLE | BR_EDR_NOT_SUPPORTED),
            AdStructure::ServiceUuids16(&[[0x34, 0x12]]),
            AdStructure::CompleteLocalName(b"ESP32S3-LoRa"),
        ],
        &mut adv_data[..],
    )
    .unwrap();

    let mut scan_data = [0; 31];
    let scan_data_len = AdStructure::encode_slice(
        &[AdStructure::CompleteLocalName(b"ESP32S3-LoRa")],
        &mut scan_data[..],
    )
    .unwrap();

    // Run the BLE runner and advertising loop concurrently
    join(ble_runner(runner), async {
        loop {
            // Advertise and wait for connection
            let acceptor = peripheral
                .advertise(
                    &Default::default(),
                    Advertisement::ConnectableScannableUndirected {
                        adv_data: &adv_data[..adv_data_len],
                        scan_data: &scan_data[..scan_data_len],
                    },
                )
                .await
                .unwrap();
            let conn = acceptor
                .accept()
                .await
                .unwrap()
                .with_attribute_server(&server)
                .unwrap();

            // Handle the GATT connection
            gatt_events_task(&server, &conn, &mut ble_to_lora, &mut lora_to_ble).await;
        }
    })
    .await;
}

/// Background task that runs the BLE stack's event loop.
/// This must run continuously alongside other BLE tasks.
async fn ble_runner(
    runner: Runner<'_, ExternalController<BleConnector<'static>, 20>, DefaultPacketPool>,
) {
    let mut runner = runner;
    runner.run().await.unwrap();
}

/// Handles GATT events for a connected BLE central.
/// Processes read/write requests and notifications for the TX/RX characteristics.
/// Forwards messages between BLE and LoRa via channels.
async fn gatt_events_task(
    server: &Server<'_>,
    conn: &GattConnection<'_, '_, DefaultPacketPool>,
    ble_to_lora: &mut Sender<'static, CriticalSectionRawMutex, Message, 1>,
    lora_to_ble: &mut Receiver<'static, CriticalSectionRawMutex, Message, 1>,
) {
    loop {
        match conn.next().await {
            GattConnectionEvent::Disconnected { .. } => break,
            GattConnectionEvent::Gatt { event } => {
                match &event {
                    GattEvent::Write(event) if event.handle() == server.lora_service.rx.handle => {
                        if let Ok(msg) = Message::deserialize(event.data()) {
                            let _ = ble_to_lora.try_send(msg);
                        }
                    }
                    GattEvent::Read(event) if event.handle() == server.lora_service.tx.handle => {
                        // Handle read requests (currently no-op for TX)
                    }
                    _ => {}
                }
            }
            _ => {}
        }

        // Check for messages from LoRa to send to BLE central
        if let Ok(msg) = lora_to_ble.try_receive() {
            let mut buf = [0u8; 512];
            if let Ok(len) = msg.serialize(&mut buf) {
                let mut data = [0u8; 512];
                data[..len].copy_from_slice(&buf[..len]);
                let _ = server.lora_service.tx.notify(conn, &data).await;
            }
        }
    }
}

#[embassy_executor::task]
/// LoRa task that manages LoRa radio operations, including transmission and reception.
/// Handles message forwarding between LoRa and BLE channels, and sends acknowledgments.
async fn lora_task(
    mut lora: LoraRadio,
    ble_to_lora: Receiver<'static, CriticalSectionRawMutex, Message, 1>,
    lora_to_ble: Sender<'static, CriticalSectionRawMutex, Message, 1>,
) {
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

static BLE_TO_LORA: StaticCell<Channel<CriticalSectionRawMutex, Message, 1>> = StaticCell::new();
static LORA_TO_BLE: StaticCell<Channel<CriticalSectionRawMutex, Message, 1>> = StaticCell::new();
static SPI_BUS: StaticCell<
    Mutex<CriticalSectionRawMutex, esp_hal::spi::master::Spi<'static, Async>>,
> = StaticCell::new();
static RADIO: StaticCell<Controller<'static>> = StaticCell::new();

type LoraRadio = LoRa<
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
