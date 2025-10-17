use crate::protocol::Message;
use bt_hci::controller::ExternalController;
use embassy_futures::join::join;
use embassy_sync::{
    blocking_mutex::raw::CriticalSectionRawMutex,
    channel::{Receiver, Sender},
};
use esp_radio::{Controller, ble::controller::BleConnector};
use log::{error, info, warn};
use trouble_host::prelude::*;
use trouble_host::{
    Address,
    gatt::{GattConnection, GattConnectionEvent, GattEvent},
    prelude::{AdStructure, gatt_service},
};

const CONNECTIONS_MAX: usize = 1;
const L2CAP_CHANNELS_MAX: usize = 1;

#[embassy_executor::task]
/// BLE task that handles BLE stack initialization, advertising, and GATT event processing.
/// Forwards messages between BLE and LoRa channels.
pub async fn ble_task(
    radio: &'static Controller<'static>,
    bt_peripheral: esp_hal::peripherals::BT<'static>,
    mut ble_to_lora: Sender<'static, CriticalSectionRawMutex, Message, 1>,
    mut lora_to_ble: Receiver<'static, CriticalSectionRawMutex, Message, 1>,
) {
    info!("BLE task starting...");

    // Initialize BLE controller
    let transport = BleConnector::new(radio, bt_peripheral, Default::default()).unwrap();
    let controller = ExternalController::<_, 20>::new(transport);
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
    info!("GATT server created with LoRa service");

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
            info!("Starting BLE advertising...");
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
            info!("BLE connection accepted");
            let conn = acceptor
                .accept()
                .await
                .unwrap()
                .with_attribute_server(&server)
                .unwrap();

            // Handle the GATT connection
            gatt_events_task(&server, &conn, &mut ble_to_lora, &mut lora_to_ble).await;
            warn!("BLE connection closed, restarting advertising");
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
    info!("GATT event handler started");
    loop {
        match conn.next().await {
            GattConnectionEvent::Disconnected { .. } => {
                info!("BLE client disconnected");
                break;
            }
            GattConnectionEvent::Gatt { event } => match &event {
                GattEvent::Write(event) if event.handle() == server.lora_service.rx.handle => {
                    info!(
                        "Received BLE write on RX characteristic, {} bytes",
                        event.data().len()
                    );
                    match Message::deserialize(event.data()) {
                        Ok(msg) => match ble_to_lora.try_send(msg) {
                            Ok(_) => info!("Message forwarded from BLE to LoRa"),
                            Err(_) => {
                                error!("Failed to send message to LoRa channel (channel full)")
                            }
                        },
                        Err(e) => error!("Failed to deserialize message from BLE: {:?}", e),
                    }
                }
                GattEvent::Read(event) if event.handle() == server.lora_service.tx.handle => {
                    // Handle read requests (currently no-op for TX)
                }
                _ => {}
            },
            _ => {}
        }

        // Check for messages from LoRa to send to BLE central
        if let Ok(msg) = lora_to_ble.try_receive() {
            info!("Received message from LoRa to forward to BLE");
            let mut buf = [0u8; 512];
            match msg.serialize(&mut buf) {
                Ok(len) => {
                    let mut data = [0u8; 512];
                    data[..len].copy_from_slice(&buf[..len]);
                    match server.lora_service.tx.notify(conn, &data).await {
                        Ok(_) => info!("Message forwarded from LoRa to BLE via notification"),
                        Err(e) => error!("Failed to send BLE notification: {:?}", e),
                    }
                }
                Err(e) => error!("Failed to serialize message for BLE: {:?}", e),
            }
        }
    }
}

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
