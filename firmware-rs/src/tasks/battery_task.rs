//! Battery monitoring task - autonomous Embassy task for periodic battery level updates.
//!
//! Reads battery voltage via ADC and sends level updates to BLE via channel.
//! Replaces BatteryMonitor and BatteryIndicator traits with direct hardware control.

use crate::constants::OCV_TABLE;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Sender;
use embassy_time::{Duration, Ticker, Timer};
use esp_hal::Blocking;
use esp_hal::analog::adc::{Adc, AdcPin};
use esp_hal::gpio::{AnyPin, Flex, InputConfig, Pull};
use esp_hal::peripherals::{ADC1, GPIO1};
use log::{debug, error, info, warn};

/// Number of ADC samples to average (matches Arduino: 15)
const BATTERY_SENSE_SAMPLES: u32 = 15;

/// Battery update interval in seconds
const BATTERY_UPDATE_INTERVAL_SECS: u64 = 60;

/// Battery monitoring task that periodically reads ADC and sends level to BLE.
/// Uses concrete GPIO1<'static> pin type (Heltec WiFi LoRa V3 battery ADC pin).
///
/// # Arguments
/// * `adc` - ADC1 peripheral for reading battery voltage
/// * `pin` - ADC pin connected to battery voltage divider (GPIO1<'static>)
/// * `divider_ratio` - Voltage divider ratio (e.g., 5.1205 for Heltec WiFi LoRa V3)
/// * `ctrl_pin` - Optional control pin to enable battery ADC circuit (GPIO37 on Heltec)
/// * `battery_sender` - Channel sender to notify BLE of battery level updates
#[embassy_executor::task]
pub async fn battery_task(
    mut adc: Adc<'static, ADC1<'static>, Blocking>,
    mut pin: AdcPin<GPIO1<'static>, ADC1<'static>>,
    divider_ratio: f32,
    ctrl_pin: Option<AnyPin<'static>>,
    battery_sender: Sender<'static, CriticalSectionRawMutex, u8, 1>,
) {
    info!("[Battery] Starting battery monitoring task");
    info!(
        "[Battery] Config: divider_ratio={:.4}, samples={}, interval={}s",
        divider_ratio, BATTERY_SENSE_SAMPLES, BATTERY_UPDATE_INTERVAL_SECS
    );

    // Initialize control pin as input with pull-down (disabled state, like Arduino)
    let mut ctrl_pin: Option<Flex<'static>> = ctrl_pin.map(|p| {
        let mut flex = Flex::new(p);
        // Start in disabled state: input with pull-down (INPUT_PULLDOWN like Arduino)
        flex.apply_input_config(&InputConfig::default().with_pull(Pull::Down));
        flex.set_output_enable(false);
        flex.set_input_enable(true);
        info!("[Battery] ADC control pin initialized (GPIO37)");
        flex
    });

    // Low-pass filtered voltage for smoothing (like Arduino)
    let mut last_voltage: f32 = 3700.0; // Start at reasonable 3.7V
    let mut initial_read_done = false;

    // Periodic battery updates using Embassy Ticker (every 60s like Arduino)
    let mut ticker = Ticker::every(Duration::from_secs(BATTERY_UPDATE_INTERVAL_SECS));

    // Initial battery check
    info!("[Battery] Taking initial reading...");
    let level = read_battery_level(
        &mut adc,
        &mut pin,
        divider_ratio,
        &mut ctrl_pin,
        &mut last_voltage,
        &mut initial_read_done,
    )
    .await;

    info!(
        "[Battery] Initial reading: {}% ({:.0} mV)",
        level, last_voltage
    );

    match battery_sender.try_send(level) {
        Ok(_) => debug!("[Battery] Initial level sent to BLE"),
        Err(_) => warn!("[Battery] Failed to send initial level (channel full)"),
    }

    loop {
        ticker.next().await;

        let level = read_battery_level(
            &mut adc,
            &mut pin,
            divider_ratio,
            &mut ctrl_pin,
            &mut last_voltage,
            &mut initial_read_done,
        )
        .await;

        debug!(
            "[Battery] Periodic reading: {}% ({:.0} mV)",
            level, last_voltage
        );

        // Send to BLE (non-blocking, overwrites old value if not consumed)
        match battery_sender.try_send(level) {
            Ok(_) => debug!("[Battery] Level update sent to BLE"),
            Err(_) => debug!("[Battery] Channel full, level not sent (normal if BLE disconnected)"),
        }
    }
}

/// Read battery level from ADC with filtering
async fn read_battery_level(
    adc: &mut Adc<'static, ADC1<'static>, Blocking>,
    pin: &mut AdcPin<GPIO1<'static>, ADC1<'static>>,
    divider_ratio: f32,
    ctrl_pin: &mut Option<Flex<'static>>,
    last_voltage: &mut f32,
    initial_read_done: &mut bool,
) -> u8 {
    let mv = read_battery_voltage(
        adc,
        pin,
        divider_ratio,
        ctrl_pin,
        last_voltage,
        initial_read_done,
    )
    .await;
    voltage_to_level(mv)
}

/// Read battery voltage from ADC with averaging and filtering
async fn read_battery_voltage(
    adc: &mut Adc<'static, ADC1<'static>, Blocking>,
    pin: &mut AdcPin<GPIO1<'static>, ADC1<'static>>,
    divider_ratio: f32,
    ctrl_pin: &mut Option<Flex<'static>>,
    last_voltage: &mut f32,
    initial_read_done: &mut bool,
) -> u16 {
    // Enable battery ADC circuit (Heltec: active LOW)
    enable_adc(ctrl_pin);

    // Allow voltage divider circuit to settle (10ms like Arduino)
    Timer::after(Duration::from_millis(10)).await;

    // Take multiple samples and average (like Arduino: 15 samples).
    // read_oneshot returns WouldBlock while conversion is in progress;
    // nb::block! spins until a value is ready (no samples dropped).
    // Yield between samples to let other tasks run.
    let mut raw_sum: u32 = 0;
    let mut valid_samples: u32 = 0;

    for _i in 0..BATTERY_SENSE_SAMPLES {
        match nb::block!(adc.read_oneshot(pin)) {
            Ok(raw) => {
                raw_sum += raw as u32;
                valid_samples += 1;
            }
            Err(_) => {
                // nb::block! only surfaces non-WouldBlock errors; error type is ()
                warn!("[Battery] ADC read error (hardware fault)");
            }
        }
        // Yield to executor between samples so higher-priority work isn't starved
        embassy_futures::yield_now().await;
    }

    // Disable battery ADC circuit to save power (INPUT_PULLDOWN like Arduino)
    disable_adc(ctrl_pin);

    if valid_samples == 0 {
        error!(
            "[Battery] No valid ADC samples! Returning last known voltage: {:.0} mV",
            *last_voltage
        );
        return *last_voltage as u16;
    }

    if valid_samples < BATTERY_SENSE_SAMPLES {
        warn!(
            "[Battery] Only {}/{} valid samples",
            valid_samples, BATTERY_SENSE_SAMPLES
        );
    }

    let raw_avg = raw_sum / valid_samples;

    // Convert to voltage
    // Using 11dB attenuation: ~0-3.1V range, 12-bit ADC (0-4095)
    // V_pin = (raw / 4095) * ~2450 mV (typical Vref at 11dB)
    let pin_mv = (raw_avg * 2450 / 4095) as u16;
    let scaled_mv = pin_mv as f32 * divider_ratio;

    debug!(
        "[Battery] ADC: raw_avg={}, pin_mv={}, scaled_mv={:.0}",
        raw_avg, pin_mv, scaled_mv
    );

    // Apply low-pass filter (like Arduino: alpha = 0.5)
    if !*initial_read_done {
        // First reading - initialize filter if value is plausible
        if scaled_mv > *last_voltage {
            *last_voltage = scaled_mv;
        }
        *initial_read_done = true;
    } else {
        // Low-pass filter: output = output + alpha * (input - output)
        *last_voltage += (scaled_mv - *last_voltage) * 0.5;
    }

    debug!("[Battery] Filtered voltage: {:.0} mV", *last_voltage);

    *last_voltage as u16
}

/// Enable battery ADC circuit (Heltec: OUTPUT LOW = enabled)
fn enable_adc(ctrl_pin: &mut Option<Flex<'static>>) {
    if let Some(ctrl) = ctrl_pin {
        // Switch to output mode, drive LOW
        ctrl.set_low(); // Set level first
        ctrl.set_input_enable(false);
        ctrl.set_output_enable(true);
    }
}

/// Disable battery ADC circuit (Arduino: INPUT_PULLDOWN)
fn disable_adc(ctrl_pin: &mut Option<Flex<'static>>) {
    if let Some(ctrl) = ctrl_pin {
        // Switch to input mode with pull-down (like Arduino's INPUT_PULLDOWN)
        ctrl.set_output_enable(false);
        ctrl.apply_input_config(&InputConfig::default().with_pull(Pull::Down));
        ctrl.set_input_enable(true);
    }
}

/// Convert millivolts to battery percentage using linear interpolation between OCV points.
/// Matches C++ PowerManager linear interpolation (not discrete 10% steps).
fn voltage_to_level(mvolts: u16) -> u8 {
    if mvolts >= OCV_TABLE[0] {
        return 100;
    }
    if mvolts <= OCV_TABLE[10] {
        return 0;
    }
    for i in 0..10 {
        if mvolts >= OCV_TABLE[i + 1] {
            // mvolts is between OCV_TABLE[i] (higher SOC) and OCV_TABLE[i+1] (lower SOC)
            let v_high = OCV_TABLE[i] as u32;
            let v_low = OCV_TABLE[i + 1] as u32;
            let v = mvolts as u32;
            let pct_high = (100 - i * 10) as u32;
            let pct_low = (100 - (i + 1) * 10) as u32;
            // Linear interpolation
            return (pct_low + (v - v_low) * (pct_high - pct_low) / (v_high - v_low)) as u8;
        }
    }
    0
}
