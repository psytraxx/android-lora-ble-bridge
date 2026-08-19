//! Deep sleep adapter - implements power-saving deep sleep with GPIO and timer wakeup
//! Deep sleep resets the CPU completely - all state is lost and main() runs fresh on wake.
//!
//! Wake sources (matching Arduino firmware):
//! - EXT0: LoRa DIO1 (GPIO 14) - wakes on HIGH (packet received)
//! - EXT1: Wake button (GPIO 0) - wakes on LOW (button press)
//! - Timer: Backup wake after specified duration
//!
//! VEXT Control (Heltec WiFi LoRa V3):
//! - GPIO 36 controls external peripherals power
//! - Set LOW before sleep to disable external peripherals for power savings

use crate::constants::heltec_wifi_lora_v3::VEXT_PIN;
use crate::ports::Sleep;
use esp_hal::{
    delay::Delay,
    gpio::{Input, InputConfig, Level, Output, OutputConfig, Pull},
    peripherals::{GPIO0, GPIO14, GPIO36, LPWR},
    rtc_cntl::{
        Rtc,
        sleep::{Ext0WakeupSource, Ext1WakeupSource, WakeupLevel},
    },
};
use log::{debug, info, warn};

/// Deep sleep adapter using ESP-HAL RTC with EXT0, EXT1, and timer wakeup
/// Matches Arduino firmware behavior: wake on LoRa DIO, button, or timer
/// Also controls VEXT pin (GPIO 36) to disable external peripherals for power savings
pub struct DeepSleepAdapter<'a> {
    rtc: Rtc<'a>,
}

impl<'a> DeepSleepAdapter<'a> {
    /// Create a new deep sleep adapter
    ///
    /// # Arguments
    /// * `rtc_cntl` - RTC peripheral (LPWR) for sleep management
    ///
    /// Note: GPIO pins for wake (GPIO0, GPIO14) and VEXT (GPIO36) are obtained via steal()
    /// before sleep because they're owned by other adapters. This is safe because
    /// deep sleep resets the device anyway.
    pub fn new(rtc_cntl: LPWR<'a>) -> Self {
        info!("[Sleep] Initializing deep sleep adapter");
        info!("[Sleep] Wake sources: EXT0=GPIO14/HIGH (LoRa), EXT1=GPIO0/LOW (button)");
        info!(
            "[Sleep] VEXT control: GPIO{} (will be set LOW before sleep)",
            VEXT_PIN
        );

        let rtc = Rtc::new(rtc_cntl);
        Self { rtc }
    }
}

impl<'a> Sleep for DeepSleepAdapter<'a> {
    fn enter_sleep(&mut self) -> ! {
        info!("[Sleep] ========================================");
        info!("[Sleep] ENTERING DEEP SLEEP");
        info!("[Sleep] Wake triggers: LoRa DIO1 HIGH, Button LOW");
        info!("[Sleep] Note: CPU will RESET on wake, all state lost");
        info!("[Sleep] ========================================");

        // Flush logs: block briefly to let UART/USB-CDC finish transmitting.
        // Cannot use embassy_time::Timer here (sync context, waker vtable mismatch).
        // Delay::new() uses CPU cycle counting — safe to call from any context.
        info!("[Sleep] Flushing logs...");
        Delay::new().delay_millis(100u32);

        // SAFETY: We're about to enter deep sleep which resets the device.
        // The GPIO pins are owned by other adapters (LoRa uses GPIO14, etc),
        // but since we're resetting anyway, stealing them is safe.
        // On wake, main() runs fresh and re-initializes everything.
        unsafe {
            // VEXT control: Set GPIO 36 LOW to disable external peripherals for power savings
            // This matches Arduino firmware behavior for Heltec WiFi LoRa V3
            info!("[Sleep] Disabling external peripherals (VEXT -> LOW)...");
            let vext_pin = GPIO36::steal();
            let mut vext_output = Output::new(vext_pin, Level::Low, OutputConfig::default());
            vext_output.set_low();
            debug!("[Sleep] VEXT (GPIO{}) set LOW", VEXT_PIN);

            // DEBUG: Check wake pin states before configuring sleep
            info!("[Sleep] Checking wake pin states...");
            {
                let check_dio = GPIO14::steal();
                let check_dio_in = Input::new(check_dio, InputConfig::default());
                let dio_state = check_dio_in.is_high();

                let check_btn = GPIO0::steal();
                // Enable pull-up for button check to verify if it's actually pressed or just floating
                let check_btn_in =
                    Input::new(check_btn, InputConfig::default().with_pull(Pull::Up));
                let btn_state = check_btn_in.is_low();

                info!(
                    "[Sleep] GPIO14 (LoRa DIO1): {} {}",
                    if dio_state { "HIGH" } else { "LOW" },
                    if dio_state {
                        "<-- WILL WAKE IMMEDIATELY!"
                    } else {
                        "(ok)"
                    }
                );
                info!(
                    "[Sleep] GPIO0 (Button): {} {}",
                    if btn_state {
                        "LOW (pressed)"
                    } else {
                        "HIGH (released)"
                    },
                    if btn_state {
                        "<-- WILL WAKE IMMEDIATELY!"
                    } else {
                        "(ok)"
                    }
                );

                if dio_state || btn_state {
                    warn!("[Sleep] WARNING: Wake pin already triggered, sleep may be very short!");
                }
            }

            // EXT0: LoRa DIO1 (GPIO 14) - wake on HIGH (packet received)
            debug!("[Sleep] Configuring EXT0 wake source (GPIO14 HIGH)...");
            let lora_dio = GPIO14::steal();
            let ext0_wakeup = Ext0WakeupSource::new(lora_dio, WakeupLevel::High);

            // EXT1: Wake button (GPIO 0) - wake on LOW (button press with pull-up)
            debug!("[Sleep] Configuring EXT1 wake source (GPIO0 LOW)...");
            let mut wake_button = GPIO0::steal();
            let ext1_pins: &mut [&mut dyn esp_hal::gpio::RtcPin] = &mut [&mut wake_button];
            let ext1_wakeup = Ext1WakeupSource::new(ext1_pins, WakeupLevel::Low);

            info!("[Sleep] Wake sources configured, entering deep sleep NOW...");

            // Enter deep sleep - device will RESET on wake, main() runs again
            // No timer wakeup — matches C++ firmware which explicitly disables timer wakeup.
            // Device sleeps indefinitely until LoRa packet or button press.
            self.rtc.sleep_deep(&[&ext0_wakeup, &ext1_wakeup]);
        }
    }
}
