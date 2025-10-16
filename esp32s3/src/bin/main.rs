//! ESP32-S3 Firmware for LoRa-BLE Bridge
//!
//! This firmware implements a BLE peripheral that communicates with Android devices
//! and is designed to bridge BLE messages to LoRa transmission (LoRa implementation pending).
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

use bt_hci::controller::ExternalController;
use embassy_executor::Spawner;
use embassy_futures::join::join;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_time::{Duration, Timer};
use esp_hal::clock::CpuClock;
use esp_hal::timer::timg::TimerGroup;
use esp_radio::ble::controller::BleConnector;
use esp32s3::protocol::Message;
use static_cell::StaticCell;
use trouble_host::prelude::*;

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}

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
    static RADIO: StaticCell<esp_radio::Controller<'static>> = StaticCell::new();
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

    // TODO: Spawn LoRa task for radio communication

    // Main loop: keep the system running
    loop {
        Timer::after(Duration::from_secs(1)).await;
    }

    // for inspiration have a look at the examples at https://github.com/esp-rs/esp-hal/tree/esp-hal-v1.0.0-rc.1/examples/src/bin
}

#[embassy_executor::task]
async fn ble_task(
    controller: ExternalController<BleConnector<'static>, 20>,
    mut ble_to_lora: embassy_sync::channel::Sender<'static, CriticalSectionRawMutex, Message, 1>,
    mut lora_to_ble: embassy_sync::channel::Receiver<'static, CriticalSectionRawMutex, Message, 1>,
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
    ble_to_lora: &mut embassy_sync::channel::Sender<'static, CriticalSectionRawMutex, Message, 1>,
    lora_to_ble: &mut embassy_sync::channel::Receiver<'static, CriticalSectionRawMutex, Message, 1>,
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

static BLE_TO_LORA: StaticCell<Channel<CriticalSectionRawMutex, Message, 1>> = StaticCell::new();
static LORA_TO_BLE: StaticCell<Channel<CriticalSectionRawMutex, Message, 1>> = StaticCell::new();
