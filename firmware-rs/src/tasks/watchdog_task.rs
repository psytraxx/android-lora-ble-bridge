//! Watchdog task - autonomous Embassy task for hardware watchdog and inactivity monitoring.
//!
//! Feeds the hardware WDT periodically and monitors activity via Signal.
//! Sends disconnect command when inactivity timeout is exceeded.

use crate::constants::INACTIVITY_TIMEOUT_MS;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Sender;
use embassy_sync::signal::Signal;
use embassy_time::{Duration, Instant, WithTimeout};
use esp_hal::peripherals::TIMG1;
use esp_hal::timer::timg::Wdt;
use log::{debug, error, info, warn};

/// Watchdog feed interval in milliseconds
const WATCHDOG_FEED_INTERVAL_MS: u64 = 500;

/// Watchdog task that feeds the hardware WDT and monitors activity.
/// Uses TIMG1 (the timer group used for watchdog on Heltec WiFi LoRa V3).
///
/// # Arguments
/// * `wdt` - Hardware watchdog timer (TIMG1)
/// * `activity_signal` - Signal updated by MessageRouter on activity
/// * `disconnect_sender` - Channel to send disconnect command to BLE
#[embassy_executor::task]
pub async fn watchdog_task(
    mut wdt: Wdt<TIMG1<'static>>,
    activity_signal: &'static Signal<CriticalSectionRawMutex, Instant>,
    disconnect_sender: Sender<'static, CriticalSectionRawMutex, (), 1>,
) {
    info!("[Watchdog] Starting watchdog task");
    info!(
        "[Watchdog] Config: feed_interval={}ms, inactivity_timeout={}ms",
        WATCHDOG_FEED_INTERVAL_MS, INACTIVITY_TIMEOUT_MS
    );

    let timeout_duration = Duration::from_millis(INACTIVITY_TIMEOUT_MS);
    let feed_interval = Duration::from_millis(WATCHDOG_FEED_INTERVAL_MS);
    let mut last_activity = Instant::now();

    loop {
        // Feed the hardware watchdog
        wdt.feed();
        debug!("[Watchdog] Hardware WDT fed");

        // Wait for activity signal or feed interval timeout — event-driven, no polling
        match activity_signal.wait().with_timeout(feed_interval).await {
            Ok(activity_time) => {
                let time_since = Instant::now().duration_since(activity_time);
                debug!(
                    "[Watchdog] Activity signal received ({} ms ago)",
                    time_since.as_millis()
                );
                last_activity = activity_time;
            }
            Err(_timeout) => {
                // No activity within feed interval — check inactivity timeout
            }
        }

        // Check for inactivity timeout
        let elapsed = Instant::now().duration_since(last_activity);
        if elapsed >= timeout_duration {
            warn!(
                "[Watchdog] INACTIVITY TIMEOUT: {} seconds without activity (threshold: {} seconds)",
                elapsed.as_secs(),
                timeout_duration.as_secs()
            );
            info!("[Watchdog] Sending disconnect command to BLE...");

            // Send disconnect command to BLE
            match disconnect_sender.try_send(()) {
                Ok(_) => {
                    info!("[Watchdog] Disconnect command sent successfully");
                }
                Err(e) => {
                    error!("[Watchdog] Failed to send disconnect command: {:?}", e);
                }
            }

            // Reset activity timer after sending disconnect
            last_activity = Instant::now();
            info!("[Watchdog] Activity timer reset");
        }
    }
}
