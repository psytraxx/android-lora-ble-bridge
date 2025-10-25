use crate::protocol::Message;
use bt_hci::controller::ExternalController;
use embassy_futures::join::join;
use embassy_sync::{
    blocking_mutex::raw::CriticalSectionRawMutex,
    channel::{Receiver, Sender},
};
use embassy_time::{Duration, Timer};
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
/// The lora_to_ble channel has capacity of 10 to buffer messages while BLE is disconnected.
pub async fn ble_task(
    radio: &'static Controller<'static>,
    bt_peripheral: esp_hal::peripherals::BT<'static>,
    mut ble_to_lora: Sender<'static, CriticalSectionRawMutex, Message, 5>,
    mut lora_to_ble: Receiver<'static, CriticalSectionRawMutex, Message, 10>,
) {
    info!("BLE task starting...");

    // Initialize BLE controller
    let transport = match BleConnector::new(radio, bt_peripheral, Default::default()) {
        Ok(t) => t,
        Err(e) => {
            error!("Failed to create BLE connector: {:?}", e);
            return; // Exit the task
        }
    };
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
    let server = match Server::new_with_config(GapConfig::Peripheral(PeripheralConfig {
        name: "ESP32S3-LoRa",
        appearance: &appearance::power_device::GENERIC_POWER_DEVICE,
    })) {
        Ok(s) => s,
        Err(e) => {
            error!("Failed to create GATT server: {:?}", e);
            return;
        }
    };
    info!("GATT server created with LoRa service");

    // Prepare advertising data
    let mut adv_data = [0; 31];
    let adv_data_len = match AdStructure::encode_slice(
        &[
            AdStructure::Flags(LE_GENERAL_DISCOVERABLE | BR_EDR_NOT_SUPPORTED),
            AdStructure::ServiceUuids16(&[[0x34, 0x12]]),
            AdStructure::CompleteLocalName(b"ESP32S3-LoRa"),
        ],
        &mut adv_data[..],
    ) {
        Ok(len) => len,
        Err(e) => {
            error!("Failed to encode advertising data: {:?}", e);
            return;
        }
    };

    let mut scan_data = [0; 31];
    let scan_data_len = match AdStructure::encode_slice(
        &[AdStructure::CompleteLocalName(b"ESP32S3-LoRa")],
        &mut scan_data[..],
    ) {
        Ok(len) => len,
        Err(e) => {
            error!("Failed to encode scan data: {:?}", e);
            return;
        }
    };

    // Run the BLE runner and advertising loop concurrently
    join(ble_runner(runner), async {
        loop {
            info!("Starting BLE advertising...");
            info!("Device name: ESP32S3-LoRa");
            info!(
                "Advertising with adv_data: {} bytes, scan_data: {} bytes",
                adv_data_len, scan_data_len
            );
            // Advertise and wait for connection
            let acceptor = match peripheral
                .advertise(
                    &Default::default(),
                    Advertisement::ConnectableScannableUndirected {
                        adv_data: &adv_data[..adv_data_len],
                        scan_data: &scan_data[..scan_data_len],
                    },
                )
                .await
            {
                Ok(a) => {
                    info!("Advertising started successfully, waiting for connection...");
                    a
                }
                Err(e) => {
                    error!("Failed to start BLE advertising: {:?}", e);
                    Timer::after(Duration::from_secs(1)).await;
                    continue;
                }
            };
            info!("BLE connection accepted");
            let conn = match acceptor.accept().await {
                Ok(c) => {
                    info!("Connection accepted successfully");
                    c
                }
                Err(e) => {
                    error!("Failed to accept BLE connection: {:?}", e);
                    continue;
                }
            };
            let conn = match conn.with_attribute_server(&server) {
                Ok(c) => c,
                Err(e) => {
                    error!("Failed to attach GATT server to connection: {:?}", e);
                    continue;
                }
            };

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
/// On reconnection, delivers all buffered messages (up to 10) that were received while disconnected.
async fn gatt_events_task(
    server: &Server<'_>,
    conn: &GattConnection<'_, '_, DefaultPacketPool>,
    ble_to_lora: &mut Sender<'static, CriticalSectionRawMutex, Message, 5>,
    lora_to_ble: &mut Receiver<'static, CriticalSectionRawMutex, Message, 10>,
) {
    info!("GATT event handler started");
    info!(
        "RX characteristic handle: {:?}",
        server.lora_service.rx.handle
    );
    info!(
        "TX characteristic handle: {:?}",
        server.lora_service.tx.handle
    );
    loop {
        info!("Waiting for GATT event...");
        match conn.next().await {
            GattConnectionEvent::Disconnected { .. } => {
                info!("BLE client disconnected");
                break;
            }
            GattConnectionEvent::Gatt { event } => {
                info!("Received GATT event");
                match &event {
                    GattEvent::Write(write_event) => {
                        info!(
                            "Write event - handle: {:?}, data length: {}",
                            write_event.handle(),
                            write_event.data().len()
                        );
                        if write_event.handle() == server.lora_service.rx.handle {
                            info!(
                                "Received BLE write on RX characteristic, {} bytes",
                                write_event.data().len()
                            );
                            match Message::deserialize(write_event.data()) {
                                Ok(msg) => {
                                    info!("Deserialized message: {:?}", msg);
                                    match ble_to_lora.try_send(msg) {
                                        Ok(_) => info!("Message forwarded from BLE to LoRa"),
                                        Err(_) => {
                                            error!(
                                                "Failed to send message to LoRa channel (channel full)"
                                            )
                                        }
                                    }
                                }
                                Err(e) => error!("Failed to deserialize message from BLE: {:?}", e),
                            }
                        } else {
                            info!(
                                "Write to different characteristic (handle: {:?})",
                                write_event.handle()
                            );
                        }
                    }
                    GattEvent::Read(read_event) => {
                        info!("Read event - handle: {:?}", read_event.handle());
                    }
                    _ => {
                        info!("Other GATT event");
                    }
                }
            }
            _ => {}
        }

        // Check for messages from LoRa to send to BLE central
        if let Ok(msg) = lora_to_ble.try_receive() {
            info!("Received message from LoRa to forward to BLE");
            let mut buf = [0u8; 64];
            match msg.serialize(&mut buf) {
                Ok(len) => {
                    info!("Sending {} bytes via BLE notification", len);
                    // Note: trouble-host notify() requires the full characteristic array.
                    // The BLE stack should handle MTU negotiation and packetization automatically.
                    // Android will negotiate a larger MTU (typically 247+ bytes) which is sufficient
                    // for our max message size of 61 bytes (11 + 50 text).
                    match server.lora_service.tx.notify(conn, &buf).await {
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
    /// Buffer size: 64 bytes (sufficient for max message: 11 bytes + 50 char text = 61 bytes)
    #[characteristic(uuid = "5678", read, write, notify, value = [0u8; 64])]
    tx: [u8; 64],
    /// RX characteristic (UUID 0x5679): Used to receive incoming messages from connected centrals.
    /// Readable, writable, and notifiable.
    /// Buffer size: 64 bytes (sufficient for max message: 11 bytes + 50 char text = 61 bytes)
    #[characteristic(uuid = "5679", read, write, notify, value = [0u8; 64])]
    rx: [u8; 64],
}
