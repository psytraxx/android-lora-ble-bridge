//! LED task - autonomous Embassy task for LED indicator control.
//!
//! Receives LED commands via channel and executes blink patterns on GPIO pin.
//! Replaces LedIndicator trait with direct hardware control.

use crate::constants::{LED_BLINK_DELAY_MS, LED_HEARTBEAT_ON_MS, LED_ON_MS};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Receiver;
use embassy_time::{Duration, Timer};
use esp_hal::gpio::{AnyPin, Level, Output, OutputConfig};
use log::{debug, info};

/// LED blink pattern
#[derive(Debug, Clone, Copy)]
pub enum LedPattern {
    /// Single blink - message received via LoRa
    SingleBlink,
    /// Double blink - message transmitted via LoRa
    DoubleBlink,
    /// Heartbeat blink - short 5ms blink every 2 seconds while active
    Heartbeat,
}

/// Command sent to the LED task via channel
#[derive(Debug, Clone, Copy)]
pub enum LedCommand {
    /// Execute a blink pattern
    Blink(LedPattern),
}

/// LED task that receives commands via channel and executes blink patterns.
///
/// # Arguments
/// * `pin` - GPIO pin for the LED (GPIO35 on Heltec WiFi LoRa V3)
/// * `receiver` - Channel receiver for LedCommand messages
#[embassy_executor::task]
pub async fn led_task(
    pin: AnyPin<'static>,
    receiver: Receiver<'static, CriticalSectionRawMutex, LedCommand, 5>,
) {
    info!("[LED] Starting LED task (GPIO35)");
    info!(
        "[LED] Timing: on={}ms, delay={}ms, heartbeat={}ms",
        LED_ON_MS, LED_BLINK_DELAY_MS, LED_HEARTBEAT_ON_MS
    );

    let mut led_pin = Output::new(pin, Level::Low, OutputConfig::default());

    loop {
        let cmd = receiver.receive().await;
        match cmd {
            LedCommand::Blink(pattern) => {
                execute_pattern(&mut led_pin, pattern).await;
            }
        }
    }
}

/// Execute a blink pattern on the LED pin
async fn execute_pattern(led_pin: &mut Output<'static>, pattern: LedPattern) {
    match pattern {
        LedPattern::SingleBlink => {
            debug!("[LED] Single blink (RX indicator)");
            single_blink(led_pin).await;
        }
        LedPattern::DoubleBlink => {
            debug!("[LED] Double blink (TX indicator)");
            single_blink(led_pin).await;
            single_blink(led_pin).await;
        }
        LedPattern::Heartbeat => {
            // Heartbeat is very frequent, only log at trace level
            heartbeat_blink(led_pin).await;
        }
    }
}

/// Perform a single blink (50ms on, 200ms off - matches Arduino)
async fn single_blink(led_pin: &mut Output<'static>) {
    led_pin.set_high();
    Timer::after(Duration::from_millis(LED_ON_MS)).await;
    led_pin.set_low();
    Timer::after(Duration::from_millis(LED_BLINK_DELAY_MS)).await;
}

/// Perform a heartbeat blink (5ms on - short flash)
async fn heartbeat_blink(led_pin: &mut Output<'static>) {
    led_pin.set_high();
    Timer::after(Duration::from_millis(LED_HEARTBEAT_ON_MS)).await;
    led_pin.set_low();
}
