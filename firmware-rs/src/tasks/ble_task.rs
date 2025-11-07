//! ESP32-specific BLE task.
//!
//! This task runs the BLE stack and GATT server for ESP32.
//! It communicates with the MessageRouter via channels.

#![allow(clippy::too_many_arguments)]

use crate::constants::{BLE_ADV_INTERVAL_MAX_MS, BLE_ADV_INTERVAL_MIN_MS, BLE_DEVICE_NAME_BASE};
use crate::protocol::Message;
use bt_hci::controller::ExternalController;
use embassy_futures::select::{Either, select};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::{Receiver, Sender};
use embassy_time::{Duration, Timer};
use esp_hal::efuse::Efuse;
use esp_radio::Controller;
use esp_radio::ble::controller::BleConnector;
use log::{debug, error, info, warn};
use trouble_host::prelude::*;
use trouble_host::{
    Address,
    advertise::AdvertisementParameters,
    gatt::{GattConnection, GattConnectionEvent, GattEvent},
};

const CONNECTIONS_MAX: usize = 1;
const L2CAP_CHANNELS_MAX: usize = 1;

/// GATT Server definition with LoRa service
#[gatt_server]
struct Server {
    lora_service: LoraService,
    battery_service: BatteryService,
}

/// Custom LoRa service with UUID 0x1234
#[gatt_service(uuid = "00001234-0000-1000-8000-00805f9b34fb")]
struct LoraService {
    /// TX characteristic (UUID 0x5678): Notify connected centrals of outgoing messages
    #[characteristic(uuid = "00005678-0000-1000-8000-00805f9b34fb", read, write, notify, value = [0u8; 64])]
    tx: [u8; 64],
    /// RX characteristic (UUID 0x5679): Receive incoming messages from centrals
    #[characteristic(uuid = "00005679-0000-1000-8000-00805f9b34fb", write, write_without_response, notify, value = [0u8; 64])]
    rx: [u8; 64],
}

/// Standard Battery Service
#[gatt_service(uuid = "0000180f-0000-1000-8000-00805f9b34fb")]
struct BatteryService {
    /// Battery Level characteristic (UUID 0x2A19)
    #[characteristic(uuid = "00002a19-0000-1000-8000-00805f9b34fb", read, notify, value = [0u8])]
    level: [u8; 1],
}

/// Device name bytes storage (max 24 chars: base name + "-" + 4 hex chars)
static mut DEVICE_NAME_BYTES: [u8; 24] = [0u8; 24];
static mut DEVICE_NAME_LEN: usize = 0;

/// ESP32 BLE task that runs the BLE stack and handles GATT events.
#[embassy_executor::task]
pub async fn esp32_ble_task(
    radio: &'static Controller<'static>,
    bt_peripheral: esp_hal::peripherals::BT<'static>,
    tx_to_ble: Receiver<'static, CriticalSectionRawMutex, Message, 10>,
    rx_from_ble: Sender<'static, CriticalSectionRawMutex, Message, 5>,
    battery_level: Receiver<'static, CriticalSectionRawMutex, u8, 1>,
    connection_state: Sender<'static, CriticalSectionRawMutex, bool, 1>,
    disconnect_cmd: Receiver<'static, CriticalSectionRawMutex, (), 1>,
) {
    info!("[BLE] Starting BLE task...");
    info!(
        "[BLE] Advertising interval: {}-{} ms",
        BLE_ADV_INTERVAL_MIN_MS, BLE_ADV_INTERVAL_MAX_MS
    );

    // MAC address for device identification
    let mac = Efuse::read_base_mac_address();
    let mac_suffix = [mac[4], mac[5]];
    info!(
        "[Boot] MAC address: {:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );

    // Build device name with MAC suffix
    unsafe {
        let base_bytes = BLE_DEVICE_NAME_BASE.as_bytes();
        let mut pos = 0;
        for &b in base_bytes {
            DEVICE_NAME_BYTES[pos] = b;
            pos += 1;
        }
        DEVICE_NAME_BYTES[pos] = b'-';
        pos += 1;
        for byte in mac_suffix {
            let high = (byte >> 4) & 0x0F;
            let low = byte & 0x0F;
            DEVICE_NAME_BYTES[pos] = if high < 10 {
                b'0' + high
            } else {
                b'A' + high - 10
            };
            pos += 1;
            DEVICE_NAME_BYTES[pos] = if low < 10 {
                b'0' + low
            } else {
                b'A' + low - 10
            };
            pos += 1;
        }
        DEVICE_NAME_LEN = pos;
    }

    // Initialize BLE controller
    info!("[BLE] Initializing BLE controller...");
    let transport = match BleConnector::new(radio, bt_peripheral, Default::default()) {
        Ok(t) => {
            debug!("[BLE] BLE connector created");
            t
        }
        Err(e) => {
            error!("[BLE] FATAL: Failed to create BLE connector: {:?}", e);
            error!("[BLE] BLE task cannot continue, returning");
            return;
        }
    };

    let controller = ExternalController::<_, 20>::new(transport);
    let address = Address::random([0xff, 0x8f, 0x1a, 0x05, 0xe4, 0xff]);
    debug!("[BLE] Using random address: {:?}", address);

    let mut resources: HostResources<DefaultPacketPool, CONNECTIONS_MAX, L2CAP_CHANNELS_MAX> =
        HostResources::new();
    let stack = trouble_host::new(controller, &mut resources).set_random_address(address);

    let Host {
        mut peripheral,
        runner,
        ..
    } = stack.build();

    // Get device name from static storage
    let (device_name_bytes, _) =
        unsafe { (&DEVICE_NAME_BYTES[..DEVICE_NAME_LEN], DEVICE_NAME_LEN) };
    let device_name_str = core::str::from_utf8(device_name_bytes).unwrap_or("HeltecLite-LoRa");
    info!("[BLE] Device name: '{}'", device_name_str);

    info!("[BLE] Creating GATT server...");
    let server = match Server::new_with_config(GapConfig::Peripheral(PeripheralConfig {
        name: device_name_str,
        appearance: &appearance::power_device::GENERIC_POWER_DEVICE,
    })) {
        Ok(s) => {
            info!("[BLE] GATT server created with LoRa and Battery services");
            s
        }
        Err(e) => {
            error!("[BLE] FATAL: Failed to create GATT server: {:?}", e);
            error!("[BLE] BLE task cannot continue, returning");
            return;
        }
    };

    // Prepare advertising data
    let mut adv_data = [0; 31];
    let adv_data_len = AdStructure::encode_slice(
        &[
            AdStructure::Flags(LE_GENERAL_DISCOVERABLE | BR_EDR_NOT_SUPPORTED),
            AdStructure::ServiceUuids16(&[[0x34, 0x12]]),
            AdStructure::CompleteLocalName(device_name_bytes),
        ],
        &mut adv_data[..],
    )
    .unwrap();

    let mut scan_data = [0; 31];
    let scan_data_len = AdStructure::encode_slice(
        &[AdStructure::CompleteLocalName(device_name_bytes)],
        &mut scan_data[..],
    )
    .unwrap();

    info!("[BLE] Advertising data prepared ({} bytes)", adv_data_len);
    info!("[BLE] BLE stack initialized, starting advertising loop...");

    // Run BLE runner and advertising loop concurrently
    embassy_futures::join::join(
        async {
            let mut runner = runner;
            runner.run().await.unwrap();
        },
        advertising_loop(
            &mut peripheral,
            &server,
            &adv_data[..adv_data_len],
            &scan_data[..scan_data_len],
            tx_to_ble,
            rx_from_ble,
            battery_level,
            connection_state,
            disconnect_cmd,
        ),
    )
    .await;
}

#[allow(clippy::too_many_arguments)]
async fn advertising_loop(
    peripheral: &mut Peripheral<
        '_,
        ExternalController<BleConnector<'static>, 20>,
        DefaultPacketPool,
    >,
    server: &Server<'_>,
    adv_data: &[u8],
    scan_data: &[u8],
    tx_to_ble: Receiver<'static, CriticalSectionRawMutex, Message, 10>,
    rx_from_ble: Sender<'static, CriticalSectionRawMutex, Message, 5>,
    battery_level: Receiver<'static, CriticalSectionRawMutex, u8, 1>,
    connection_state: Sender<'static, CriticalSectionRawMutex, bool, 1>,
    disconnect_cmd: Receiver<'static, CriticalSectionRawMutex, (), 1>,
) {
    let mut connection_count: u32 = 0;

    loop {
        info!("[BLE] Starting advertising (waiting for connection)...");

        let adv_params = AdvertisementParameters {
            interval_min: Duration::from_millis(BLE_ADV_INTERVAL_MIN_MS),
            interval_max: Duration::from_millis(BLE_ADV_INTERVAL_MAX_MS),
            ..Default::default()
        };

        let acceptor = match peripheral
            .advertise(
                &adv_params,
                Advertisement::ConnectableScannableUndirected {
                    adv_data,
                    scan_data,
                },
            )
            .await
        {
            Ok(a) => a,
            Err(e) => {
                error!("[BLE] Failed to start advertising: {:?}", e);
                info!("[BLE] Retrying in 1 second...");
                Timer::after(Duration::from_secs(1)).await;
                continue;
            }
        };

        debug!("[BLE] Advertising started, waiting for connection...");

        let conn = match acceptor.accept().await {
            Ok(c) => c,
            Err(e) => {
                error!("[BLE] Failed to accept connection: {:?}", e);
                continue;
            }
        };

        let conn = match conn.with_attribute_server(server) {
            Ok(c) => c,
            Err(e) => {
                error!("[BLE] Failed to attach GATT server to connection: {:?}", e);
                continue;
            }
        };

        connection_count += 1;
        info!("[BLE] ========================================");
        info!("[BLE] CONNECTION #{} ESTABLISHED", connection_count);
        info!("[BLE] ========================================");

        match connection_state.try_send(true) {
            Ok(_) => debug!("[BLE] Connection state (connected) sent to router"),
            Err(_) => warn!("[BLE] Failed to send connection state (channel full)"),
        }

        gatt_events_loop(
            server,
            &conn,
            tx_to_ble,
            rx_from_ble,
            battery_level,
            disconnect_cmd,
            connection_count,
        )
        .await;

        match connection_state.try_send(false) {
            Ok(_) => debug!("[BLE] Connection state (disconnected) sent to router"),
            Err(_) => warn!("[BLE] Failed to send disconnection state (channel full)"),
        }

        info!("[BLE] ========================================");
        info!("[BLE] CONNECTION #{} CLOSED", connection_count);
        info!("[BLE] ========================================");
    }
}

async fn gatt_events_loop(
    server: &Server<'_>,
    conn: &GattConnection<'_, '_, DefaultPacketPool>,
    tx_to_ble: Receiver<'static, CriticalSectionRawMutex, Message, 10>,
    rx_from_ble: Sender<'static, CriticalSectionRawMutex, Message, 5>,
    battery_level: Receiver<'static, CriticalSectionRawMutex, u8, 1>,
    disconnect_cmd: Receiver<'static, CriticalSectionRawMutex, (), 1>,
    conn_num: u32,
) {
    debug!(
        "[BLE] Entering GATT event loop for connection #{}",
        conn_num
    );

    loop {
        let gatt_event = conn.next();
        let outgoing_msg = tx_to_ble.receive();
        let battery_update = battery_level.receive();
        let disconnect = disconnect_cmd.receive();

        match select(
            select(gatt_event, outgoing_msg),
            select(battery_update, disconnect),
        )
        .await
        {
            Either::First(Either::First(event)) => match event {
                GattConnectionEvent::Disconnected { reason } => {
                    info!("[BLE] Disconnected event received: {:?}", reason);
                    break;
                }
                GattConnectionEvent::Gatt { event } => match event {
                    GattEvent::Write(write_event) => {
                        let handle = write_event.handle();
                        let data = write_event.data();
                        debug!(
                            "[BLE] GATT Write event: handle={}, {} bytes",
                            handle,
                            data.len()
                        );

                        if handle == server.lora_service.rx.handle {
                            match Message::deserialize(data) {
                                Ok(msg) => {
                                    info!("[BLE] Received message from client: {:?}", msg);
                                    match rx_from_ble.try_send(msg) {
                                        Ok(_) => {
                                            debug!("[BLE] Message forwarded to router");
                                        }
                                        Err(_) => {
                                            error!(
                                                "[BLE] Router RX channel FULL, message DROPPED!"
                                            );
                                        }
                                    }
                                }
                                Err(e) => {
                                    error!(
                                        "[BLE] Failed to deserialize message from client: {}",
                                        e
                                    );
                                    debug!("[BLE] Raw data ({} bytes): {:02X?}", data.len(), data);
                                }
                            }
                        } else {
                            debug!("[BLE] Write to unknown handle {}", handle);
                        }

                        if let Err(e) = write_event.accept().map(|r| r.send()) {
                            warn!("[BLE] Failed to accept write event: {:?}", e);
                        }
                    }
                    GattEvent::Read(read_event) => {
                        debug!("[BLE] GATT Read event: handle={}", read_event.handle());
                        if let Err(e) = read_event.accept().map(|r| r.send()) {
                            warn!("[BLE] Failed to accept read event: {:?}", e);
                        }
                    }
                    GattEvent::Other(other_event) => {
                        debug!("[BLE] GATT Other event");
                        if let Err(e) = other_event.accept().map(|r| r.send()) {
                            warn!("[BLE] Failed to accept other event: {:?}", e);
                        }
                    }
                },
                _ => {
                    debug!("[BLE] Unhandled GATT connection event");
                }
            },
            Either::First(Either::Second(msg)) => {
                info!("[BLE] Sending message to client: {:?}", msg);
                let mut buf = [0u8; 64];
                match msg.serialize(&mut buf) {
                    Ok(size) => {
                        debug!("[BLE] Serialized {} bytes for notification", size);
                        // Note: trouble-host requires full [u8; 64] array type.
                        // Protocol handles trailing zeros correctly (reads based on length fields).
                        match server.lora_service.tx.notify(conn, &buf).await {
                            Ok(_) => {
                                info!("[BLE] Notification sent successfully ({} bytes)", size);
                            }
                            Err(e) => {
                                error!("[BLE] Failed to send notification: {:?}", e);
                            }
                        }
                    }
                    Err(e) => {
                        error!("[BLE] Failed to serialize message for notification: {}", e);
                    }
                }
            }
            Either::Second(Either::First(level)) => {
                debug!("[BLE] Sending battery level update: {}%", level);
                match server.battery_service.level.notify(conn, &[level]).await {
                    Ok(_) => {
                        debug!("[BLE] Battery level notification sent");
                    }
                    Err(e) => {
                        warn!("[BLE] Failed to notify battery level: {:?}", e);
                    }
                }
            }
            Either::Second(Either::Second(_)) => {
                warn!("[BLE] Disconnect command received from watchdog");
                info!("[BLE] Closing connection due to inactivity timeout");
                break;
            }
        }
    }

    debug!("[BLE] Exiting GATT event loop for connection #{}", conn_num);
}
