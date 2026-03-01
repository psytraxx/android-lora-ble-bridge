//! ESP32-S3 Firmware for LoRa-BLE Bridge
//!
//! Refactored to use channel-based task architecture.
//! MessageRouter is now pure routing logic, decoupled from hardware traits.

#![no_std]
#![no_main]
#![deny(
    clippy::mem_forget,
    reason = "mem::forget is generally not safe to do with esp_hal types, especially those \
holding buffers for the duration of a data transfer."
)]

use embassy_executor::Spawner;
use esp_alloc::heap_allocator;
use esp_backtrace as _;
use esp_hal::Config;
use esp_hal::analog::adc::{Adc, AdcConfig, Attenuation};
use esp_hal::clock::CpuClock;
use esp_hal::gpio::Pin;
use esp_hal::rtc_cntl::{reset_reason, wakeup_cause};
use esp_hal::system::Cpu;
use esp_hal::timer::timg::{MwdtStage, TimerGroup};
use firmware_rs::adapters::deep_sleep_adapter::DeepSleepAdapter;
use firmware_rs::adapters::nvs_storage_adapter::NvsStorageAdapter;
use firmware_rs::channels::Channels;
use firmware_rs::domain::message_router::MessageRouter;
use firmware_rs::tasks::ble_task::esp32_ble_task;
use firmware_rs::tasks::lora_task::{LoraGpios, esp32_lora_task};
use firmware_rs::tasks::{battery_task, led_task, watchdog_task};
use log::{debug, error, info};
use static_cell::StaticCell;

extern crate alloc;

esp_bootloader_esp_idf::esp_app_desc!();

#[esp_rtos::main]
async fn main(spawner: Spawner) -> ! {
    // ========================================
    // BOOT SEQUENCE START
    // ========================================
    let config = Config::default().with_cpu_clock(CpuClock::_80MHz);
    let peripherals = esp_hal::init(config);
    esp_println::logger::init_logger_from_env();
    heap_allocator!(#[unsafe(link_section = ".dram2_uninit")] size: 73744);

    info!("========================================");
    info!("ESP32-S3 LoRa-BLE Bridge");
    info!("Firmware: Rust (Channel-based Architecture)");
    info!("========================================");

    let wake_reason = wakeup_cause();
    let reset = reset_reason(Cpu::ProCpu);
    info!("[Boot] Reset reason: {:?}", reset);
    info!("[Boot] Wake source: {:?}", wake_reason);
    info!("[Boot] CPU clock: {:?} MHz", CpuClock::_80MHz);
    let is_lora_wakeup = matches!(wake_reason, esp_hal::system::SleepSource::Ext0);

    // ========================================
    // TIMER AND WATCHDOG INITIALIZATION
    // ========================================
    info!("[Boot] Initializing timers...");

    let timg0 = TimerGroup::new(peripherals.TIMG0);
    let mut timg0_wdt = timg0.wdt;
    timg0_wdt.disable();
    esp_rtos::start(timg0.timer0);
    debug!("[Boot] TIMG0: RTOS timer started, WDT disabled");

    let timg1 = TimerGroup::new(peripherals.TIMG1);
    let mut wdt = timg1.wdt;
    wdt.set_timeout(MwdtStage::Stage0, esp_hal::time::Duration::from_secs(10));
    wdt.enable();
    info!("[Boot] Hardware watchdog enabled (10s timeout)");

    // ========================================
    // RADIO INITIALIZATION
    // ========================================
    info!("[Boot] Initializing radio subsystem...");

    let radio_init = match esp_radio::init() {
        Ok(r) => {
            debug!("[Boot] Radio controller initialized");
            r
        }
        Err(e) => {
            error!("[Boot] FATAL: Failed to initialize radio: {:?}", e);
            panic!("Radio init failed");
        }
    };
    let radio = RADIO.init(radio_init);

    // ========================================
    // CHANNEL INITIALIZATION
    // ========================================
    info!("[Boot] Creating inter-task channels...");
    let ch = CHANNELS.init(Channels::new());
    debug!(
        "[Boot] Channels: BLE_TX(10), BLE_RX(5), LORA_TX(5), LORA_RX(3), LED(5), BAT(1), CONN(1), DISC(1)"
    );

    // ========================================
    // TASK SPAWNING
    // ========================================
    info!("[Boot] Spawning tasks...");

    // Spawn BLE task
    match spawner.spawn(esp32_ble_task(
        radio,
        peripherals.BT,
        ch.ble_tx.receiver(),
        ch.ble_rx.sender(),
        ch.bat_level.receiver(),
        ch.conn_state.sender(),
        ch.disconn_cmd.receiver(),
        &ch.radio_stats,
    )) {
        Ok(_) => info!("[Boot] Task spawned: BLE"),
        Err(e) => {
            error!("[Boot] FATAL: Failed to spawn BLE task: {:?}", e);
            panic!("BLE task spawn failed");
        }
    }

    // Spawn LoRa task
    let lora_gpios = LoraGpios {
        cs: peripherals.GPIO8.degrade(),
        reset: peripherals.GPIO12.degrade(),
        dio1: peripherals.GPIO14.degrade(),
        busy: peripherals.GPIO13.degrade(),
        sck: peripherals.GPIO9.degrade(),
        miso: peripherals.GPIO11.degrade(),
        mosi: peripherals.GPIO10.degrade(),
    };
    debug!("[Boot] LoRa GPIOs: CS=8, RST=12, DIO1=14, BUSY=13, SCK=9, MISO=11, MOSI=10");

    match spawner.spawn(esp32_lora_task(
        peripherals.SPI2,
        lora_gpios,
        ch.lora_tx.receiver(),
        ch.lora_rx.sender(),
        is_lora_wakeup,
    )) {
        Ok(_) => info!("[Boot] Task spawned: LoRa"),
        Err(e) => {
            error!("[Boot] FATAL: Failed to spawn LoRa task: {:?}", e);
            panic!("LoRa task spawn failed");
        }
    }

    // Spawn LED task
    match spawner.spawn(led_task(
        peripherals.GPIO35.degrade(),
        ch.led_cmd.receiver(),
    )) {
        Ok(_) => info!("[Boot] Task spawned: LED (GPIO35)"),
        Err(e) => {
            error!("[Boot] FATAL: Failed to spawn LED task: {:?}", e);
            panic!("LED task spawn failed");
        }
    }

    // Spawn Battery task
    let mut adc1_config = AdcConfig::new();
    let battery_pin = adc1_config.enable_pin(peripherals.GPIO1, Attenuation::_11dB);
    let adc1 = Adc::new(peripherals.ADC1, adc1_config);
    debug!("[Boot] Battery ADC: GPIO1 with 11dB attenuation, control=GPIO37");

    match spawner.spawn(battery_task(
        adc1,
        battery_pin,
        5.1205,
        Some(peripherals.GPIO37.degrade()),
        ch.bat_level.sender(),
    )) {
        Ok(_) => info!("[Boot] Task spawned: Battery"),
        Err(e) => {
            error!("[Boot] FATAL: Failed to spawn Battery task: {:?}", e);
            panic!("Battery task spawn failed");
        }
    }

    // Spawn Watchdog task
    match spawner.spawn(watchdog_task(wdt, &ch.activity, ch.disconn_cmd.sender())) {
        Ok(_) => info!("[Boot] Task spawned: Watchdog"),
        Err(e) => {
            error!("[Boot] FATAL: Failed to spawn Watchdog task: {:?}", e);
            panic!("Watchdog task spawn failed");
        }
    }

    // ========================================
    // ADAPTER INITIALIZATION
    // ========================================
    info!("[Boot] Initializing adapters...");

    let storage = STORAGE.init(NvsStorageAdapter::new(peripherals.FLASH));
    info!("[Boot] NVS storage adapter initialized");

    let sleep = SLEEP.init(DeepSleepAdapter::new(peripherals.LPWR));
    info!("[Boot] Deep sleep adapter initialized");

    // ========================================
    // MESSAGE ROUTER START
    // ========================================
    info!("[Boot] Creating message router...");

    let mut router = MessageRouter::new(
        ch.ble_tx.sender(),
        ch.ble_rx.receiver(),
        ch.conn_state.receiver(),
        ch.lora_tx.sender(),
        ch.lora_rx.receiver(),
        ch.led_cmd.sender(),
        &ch.activity,
        &ch.radio_stats,
        storage,
        sleep,
    );

    let woken_by_lora = matches!(wake_reason, esp_hal::system::SleepSource::Ext0);

    info!("========================================");
    info!("[Boot] BOOT COMPLETE - Starting router");
    info!("========================================");

    router.run(woken_by_lora).await
}

// Static storage
static RADIO: StaticCell<esp_radio::Controller<'static>> = StaticCell::new();
static CHANNELS: StaticCell<Channels> = StaticCell::new();
static STORAGE: StaticCell<NvsStorageAdapter<'static>> = StaticCell::new();
static SLEEP: StaticCell<DeepSleepAdapter<'static>> = StaticCell::new();
