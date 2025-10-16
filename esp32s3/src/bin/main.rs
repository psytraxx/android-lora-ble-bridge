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
#[gatt_server]
struct Server {
    lora_service: LoraService,
}

#[gatt_service(uuid = "1234")]
struct LoraService {
    #[characteristic(uuid = "5678", read, write, notify, value = [0u8; 512])]
    tx: [u8; 512],
    #[characteristic(uuid = "5679", read, write, notify, value = [0u8; 512])]
    rx: [u8; 512],
}

#[esp_rtos::main]
async fn main(spawner: Spawner) -> ! {
    // generator version: 0.6.0

    let config = esp_hal::Config::default().with_cpu_clock(CpuClock::max());
    let peripherals = esp_hal::init(config);

    esp_alloc::heap_allocator!(#[unsafe(link_section = ".dram2_uninit")] size: 73744);

    let timg0 = TimerGroup::new(peripherals.TIMG0);
    esp_rtos::start(timg0.timer0);

    let radio_init = esp_radio::init().expect("Failed to initialize Wi-Fi/BLE controller");
    static RADIO: StaticCell<esp_radio::Controller<'static>> = StaticCell::new();
    let radio = RADIO.init(radio_init);
    // find more examples https://github.com/embassy-rs/trouble/tree/main/examples/esp32
    let transport = BleConnector::new(radio, peripherals.BT, Default::default()).unwrap();
    let ble_controller = ExternalController::<_, 20>::new(transport);

    // Init channels
    let ble_to_lora = BLE_TO_LORA.init(Channel::new());
    let lora_to_ble = LORA_TO_BLE.init(Channel::new());

    // Spawn BLE task
    spawner
        .spawn(ble_task(
            ble_controller,
            ble_to_lora.sender(),
            lora_to_ble.receiver(),
        ))
        .unwrap();

    // TODO: Spawn LoRa task

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
    let address: Address = Address::random([0xff, 0x8f, 0x1a, 0x05, 0xe4, 0xff]);
    let mut resources: HostResources<DefaultPacketPool, CONNECTIONS_MAX, L2CAP_CHANNELS_MAX> =
        HostResources::new();
    let stack = trouble_host::new(controller, &mut resources).set_random_address(address);
    let Host {
        mut peripheral,
        runner,
        ..
    } = stack.build();

    let server = Server::new_with_config(GapConfig::Peripheral(PeripheralConfig {
        name: "ESP32S3-LoRa",
        appearance: &appearance::power_device::GENERIC_POWER_DEVICE,
    }))
    .unwrap();

    // Start advertising
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

    let _ = join(ble_runner(runner), async {
        loop {
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

            // Handle connection
            gatt_events_task(&server, &conn, &mut ble_to_lora, &mut lora_to_ble).await;
        }
    });
}

async fn ble_runner(
    runner: Runner<'_, ExternalController<BleConnector<'static>, 20>, DefaultPacketPool>,
) {
    let mut runner = runner;
    runner.run().await.unwrap();
}

async fn gatt_events_task<'a>(
    server: &Server<'a>,
    conn: &GattConnection<'_, '_, DefaultPacketPool>,
    ble_to_lora: &mut embassy_sync::channel::Sender<'static, CriticalSectionRawMutex, Message, 1>,
    lora_to_ble: &mut embassy_sync::channel::Receiver<'static, CriticalSectionRawMutex, Message, 1>,
) {
    loop {
        match conn.next().await {
            GattConnectionEvent::Disconnected { .. } => break,
            GattConnectionEvent::Gatt { event } => {
                match &event {
                    GattEvent::Write(event) => {
                        if event.handle() == server.lora_service.rx.handle {
                            // Received data from BLE, send to LoRa
                            if let Ok(msg) = Message::deserialize(event.data()) {
                                let _ = ble_to_lora.try_send(msg);
                            }
                        }
                    }
                    GattEvent::Read(event) => {
                        if event.handle() == server.lora_service.tx.handle {
                            // For read, we can return the current tx buffer, but since it's notify, maybe not needed
                        }
                    }
                    _ => {}
                }
            }
            _ => {}
        }

        // Check for messages from LoRa to send to BLE
        if let Ok(msg) = lora_to_ble.try_receive() {
            let mut buf = [0u8; 512];
            if let Ok(_len) = msg.serialize(&mut buf) {
                let _ = server.lora_service.tx.notify(conn, &buf).await;
            }
        }
    }
}

static BLE_TO_LORA: StaticCell<Channel<CriticalSectionRawMutex, Message, 1>> = StaticCell::new();
static LORA_TO_BLE: StaticCell<Channel<CriticalSectionRawMutex, Message, 1>> = StaticCell::new();
