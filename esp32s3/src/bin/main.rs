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

use embassy_executor::Spawner;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_time::{Duration, Timer};
use esp_alloc::heap_allocator;
use esp_hal::clock::CpuClock;
use esp_hal::timer::timg::TimerGroup;
use esp32s3::ble::ble_task;
use esp32s3::lora::{LoraGpios, lora_task};
use esp32s3::protocol::Message;

use esp_hal::Config;
use esp_println::logger::init_logger_from_env;
use static_cell::StaticCell;

extern crate alloc;

// This creates a default app-descriptor required by the esp-idf bootloader.
// For more information see: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/app_image_format.html#application-description>
esp_bootloader_esp_idf::esp_app_desc!();

#[esp_rtos::main]
async fn main(spawner: Spawner) -> ! {
    // Initialize ESP32-S3 peripherals and clock
    let config = Config::default().with_cpu_clock(CpuClock::max());
    let peripherals = esp_hal::init(config);

    // Allocate heap memory for dynamic allocations
    heap_allocator!(#[unsafe(link_section = ".dram2_uninit")] size: 73744);
    // Initialize logging
    init_logger_from_env();

    // Initialize the RTOS timer
    let timg0 = TimerGroup::new(peripherals.TIMG0);
    esp_rtos::start(timg0.timer0);

    // Initialize Wi-Fi/BLE radio
    let radio_init = esp_radio::init().expect("Failed to initialize Wi-Fi/BLE controller");
    let radio = RADIO.init(radio_init);

    // Initialize communication channels between BLE and LoRa tasks
    let ble_to_lora = BLE_TO_LORA.init(Channel::new());
    let lora_to_ble = LORA_TO_BLE.init(Channel::new());

    // Spawn the BLE task with radio and BT peripheral
    spawner
        .spawn(ble_task(
            radio,
            peripherals.BT,
            ble_to_lora.sender(),
            lora_to_ble.receiver(),
        ))
        .unwrap();

    // Spawn LoRa task with SPI peripheral and GPIO pins
    spawner
        .spawn(lora_task(
            peripherals.SPI2,
            LoraGpios {
                cs: peripherals.GPIO5,
                reset: peripherals.GPIO12,
                dio0: peripherals.GPIO15,
                sck: peripherals.GPIO18,
                miso: peripherals.GPIO19,
                mosi: peripherals.GPIO21,
            },
            ble_to_lora.receiver(),
            lora_to_ble.sender(),
        ))
        .unwrap();

    // Main loop: keep the system running
    loop {
        Timer::after(Duration::from_secs(1)).await;
    }
}

static BLE_TO_LORA: StaticCell<Channel<CriticalSectionRawMutex, Message, 1>> = StaticCell::new();
static LORA_TO_BLE: StaticCell<Channel<CriticalSectionRawMutex, Message, 1>> = StaticCell::new();
static RADIO: StaticCell<esp_radio::Controller<'static>> = StaticCell::new();
