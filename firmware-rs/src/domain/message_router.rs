//! Message router - core business logic for routing messages between transports.
//! This is the heart of the channel-based architecture's domain layer.
//! It orchestrates communication between short-range (BLE) and long-range (LoRa) tasks
//! via Embassy channels, completely decoupled from hardware.

use crate::{
    adapters::deep_sleep_adapter::DeepSleepAdapter,
    adapters::nvs_storage_adapter::NvsStorageAdapter,
    constants::{
        ACK_JITTER_MAX_MS, ACK_JITTER_MIN_MS, ADVERTISING_DURATION_SECS, BUFFER_DRAIN_DELAY_MS,
        LED_HEARTBEAT_INTERVAL_MS,
    },
    ports::Sleep as SleepTrait,
    ports::Storage as StorageTrait,
    protocol::{AckMessage, Message},
    tasks::{LedCommand, LedPattern, RadioMetadata},
};
use embassy_futures::select::{Either3, select3};
use embassy_sync::channel::{Receiver, Sender};
use embassy_sync::signal::Signal;
use embassy_sync::{blocking_mutex::raw::CriticalSectionRawMutex, channel::TrySendError};
use embassy_time::{Duration, Instant, Ticker, Timer};
use log::{debug, error, info, warn};

/// Connection state for the short-range transport
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnectionState {
    Disconnected,
    Connected,
}

/// Message router service that bridges short-range and long-range transports.
/// This version has ZERO generic parameters and uses channels for all I/O.
pub struct MessageRouter {
    // BLE channels
    tx_to_ble: Sender<'static, CriticalSectionRawMutex, Message, 10>,
    rx_from_ble: Receiver<'static, CriticalSectionRawMutex, Message, 5>,
    connection_state_rx: Receiver<'static, CriticalSectionRawMutex, bool, 1>,

    // LoRa channels
    tx_to_lora: Sender<'static, CriticalSectionRawMutex, Message, 5>,
    rx_from_lora: Receiver<'static, CriticalSectionRawMutex, (Message, RadioMetadata), 3>,

    // Control channels
    led_commands: Sender<'static, CriticalSectionRawMutex, LedCommand, 5>,
    activity_signal: &'static Signal<CriticalSectionRawMutex, Instant>,

    // Storage (concrete adapter reference)
    storage: &'static mut NvsStorageAdapter<'static>,

    // Sleep (concrete adapter reference)
    sleep: &'static mut DeepSleepAdapter<'static>,

    // Internal state
    connection_state: ConnectionState,
}

impl MessageRouter {
    /// Create a new message router with the given channel endpoints and adapters
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        tx_to_ble: Sender<'static, CriticalSectionRawMutex, Message, 10>,
        rx_from_ble: Receiver<'static, CriticalSectionRawMutex, Message, 5>,
        connection_state_rx: Receiver<'static, CriticalSectionRawMutex, bool, 1>,
        tx_to_lora: Sender<'static, CriticalSectionRawMutex, Message, 5>,
        rx_from_lora: Receiver<'static, CriticalSectionRawMutex, (Message, RadioMetadata), 3>,
        led_commands: Sender<'static, CriticalSectionRawMutex, LedCommand, 5>,
        activity_signal: &'static Signal<CriticalSectionRawMutex, Instant>,
        storage: &'static mut NvsStorageAdapter<'static>,
        sleep: &'static mut DeepSleepAdapter<'static>,
    ) -> Self {
        info!("[Router] Initializing MessageRouter (channel-based architecture)");
        info!("[Router] Channel capacities: BLE TX=10, BLE RX=5, LoRa TX=5, LoRa RX=3");
        info!("[Router] Buffered messages in storage: {}", storage.count());

        Self {
            tx_to_ble,
            rx_from_ble,
            connection_state_rx,
            tx_to_lora,
            rx_from_lora,
            led_commands,
            activity_signal,
            storage,
            sleep,
            connection_state: ConnectionState::Disconnected,
        }
    }

    /// Signal activity to the watchdog task
    fn signal_activity(&self) {
        self.activity_signal.signal(Instant::now());
    }

    /// Run the message router loop indefinitely
    pub async fn run(&mut self, woken_by_lora: bool) -> ! {
        info!("[Router] Starting message router loop...");
        info!(
            "[Router] Wake source: {}",
            if woken_by_lora {
                "LoRa DIO"
            } else {
                "Boot/Button"
            }
        );

        // If woken by LoRa, check for packet immediately
        if woken_by_lora {
            info!("[Router] Checking for incoming LoRa packet (wake trigger)...");
            match self.rx_from_lora.try_receive() {
                Ok((msg, metadata)) => {
                    info!(
                        "[Router] Received wake-up packet: {:?} (RSSI: {}, SNR: {})",
                        msg, metadata.rssi, metadata.snr
                    );
                    self.signal_activity();
                    self.handle_long_range_message(msg).await;
                }
                Err(_) => {
                    warn!(
                        "[Router] No LoRa packet available despite wake signal (may have been processed)"
                    );
                }
            }
        }

        // Advertising phase
        info!(
            "[Router] Entering BLE advertising phase ({} seconds timeout)...",
            ADVERTISING_DURATION_SECS
        );

        let start_time = Instant::now();
        let mut connected = false;
        let mut last_heartbeat = Instant::now();

        while start_time.elapsed().as_secs() < ADVERTISING_DURATION_SECS {
            // Heartbeat LED
            if last_heartbeat.elapsed().as_millis() >= LED_HEARTBEAT_INTERVAL_MS {
                if self
                    .led_commands
                    .try_send(LedCommand::Blink(LedPattern::Heartbeat))
                    .is_err()
                {
                    debug!("[Router] LED command queue full (heartbeat)");
                }
                last_heartbeat = Instant::now();
            }

            // Wait for connection signal, LoRa message, or small timeout
            match select3(
                self.connection_state_rx.receive(),
                self.rx_from_lora.receive(),
                Timer::after(Duration::from_millis(100)),
            )
            .await
            {
                Either3::First(is_connected) => {
                    if is_connected {
                        info!("[Router] BLE connection established!");
                        self.connection_state = ConnectionState::Connected;
                        connected = true;
                        break;
                    } else {
                        debug!("[Router] Received disconnected state during advertising");
                    }
                }
                Either3::Second((msg, metadata)) => {
                    info!(
                        "[Router] LoRa message received during advertising: {:?} (RSSI: {})",
                        msg, metadata.rssi
                    );
                    self.signal_activity();
                    self.handle_long_range_message(msg).await;
                }
                Either3::Third(_) => {
                    // Timeout - continue checking
                }
            }
        }

        if connected {
            info!("[Router] Starting connected routing loop...");
            self.routing_loop().await;
            self.connection_state = ConnectionState::Disconnected;
            warn!("[Router] BLE disconnected, preparing for sleep...");
        } else {
            info!(
                "[Router] Advertising timeout after {} seconds, no connection",
                ADVERTISING_DURATION_SECS
            );
        }

        info!("[Router] Entering deep sleep (wake sources: LoRa DIO, button)...");
        info!(
            "[Router] Buffered messages remaining: {}",
            self.storage.count()
        );
        self.sleep.enter_sleep();
        // Note: Deep sleep resets the CPU, so we never get here
    }

    /// Main routing loop while connected
    async fn routing_loop(&mut self) {
        self.signal_activity();
        let mut heartbeat_ticker = Ticker::every(Duration::from_millis(LED_HEARTBEAT_INTERVAL_MS));

        // Wait for client to enable notifications before draining buffer.
        // The client needs time to discover services and write to CCCD.
        //
        // Arduino achieves this by polling `areNotificationsEnabled()` every 20ms
        // and only sending when it returns true (tracking CCCD write state).
        //
        // Proper fix: Add a signal from BLE task when TX CCCD is written,
        // then wait for that signal here instead of a fixed delay.
        // For now, use a conservative delay as a working workaround.
        info!(
            "[Router] Waiting {}ms for client to enable notifications...",
            BUFFER_DRAIN_DELAY_MS
        );
        Timer::after(Duration::from_millis(BUFFER_DRAIN_DELAY_MS)).await;
        self.signal_activity();

        // Drain buffered messages on connect
        let buffered_count = self.storage.count();
        if buffered_count > 0 {
            info!(
                "[Router] Draining {} buffered messages to BLE...",
                buffered_count
            );

            let mut sent = 0;
            let mut failed = 0;

            while let Ok(Some(msg)) = self.storage.peek() {
                // Log before sending since we'll move the message
                info!(
                    "[Router] Sending buffered message {}/{} to BLE: {:?}",
                    sent + 1,
                    buffered_count,
                    msg
                );

                // peek() returns owned Message, so no clone needed
                match self.tx_to_ble.try_send(msg) {
                    Ok(_) => {
                        match self.storage.pop() {
                            Ok(_) => {
                                sent += 1;
                            }
                            Err(e) => {
                                error!(
                                    "[Router] Failed to pop message from storage after sending: {:?}",
                                    e
                                );
                                failed += 1;
                                break;
                            }
                        }
                        // Small delay between messages to avoid overwhelming BLE
                        Timer::after(Duration::from_millis(50)).await;
                    }
                    Err(_) => {
                        warn!(
                            "[Router] BLE TX channel full, stopping buffer drain ({} sent, {} remaining)",
                            sent,
                            self.storage.count()
                        );
                        failed += 1;
                        break;
                    }
                }
            }

            info!(
                "[Router] Buffer drain complete: {} sent, {} failed, {} remaining",
                sent,
                failed,
                self.storage.count()
            );
        } else {
            debug!("[Router] No buffered messages to drain");
        }

        info!("[Router] Entering main routing loop (BLE connected)...");

        loop {
            match select3(
                self.rx_from_ble.receive(),
                self.rx_from_lora.receive(),
                heartbeat_ticker.next(),
            )
            .await
            {
                Either3::First(msg) => {
                    info!("[Router] BLE -> LoRa: {:?}", msg);
                    self.signal_activity();
                    self.handle_short_range_message(msg).await;
                }
                Either3::Second((msg, metadata)) => {
                    info!(
                        "[Router] LoRa -> BLE: {:?} (RSSI: {}, SNR: {})",
                        msg, metadata.rssi, metadata.snr
                    );
                    self.signal_activity();
                    self.handle_long_range_message(msg).await;
                }
                Either3::Third(_) => {
                    if self
                        .led_commands
                        .try_send(LedCommand::Blink(LedPattern::Heartbeat))
                        .is_err()
                    {
                        debug!("[Router] LED command queue full (heartbeat)");
                    }
                }
            }

            // Check for disconnection
            if let Ok(is_connected) = self.connection_state_rx.try_receive()
                && !is_connected
            {
                info!("[Router] BLE disconnection signal received");
                self.connection_state = ConnectionState::Disconnected;
                break;
            }
        }

        info!("[Router] Exiting routing loop");
    }

    async fn handle_short_range_message(&mut self, msg: Message) {
        info!("[Router] Forwarding message from BLE to LoRa: {:?}", msg);

        self.tx_to_lora.send(msg).await;
        debug!("[Router] Message queued for LoRa TX");

        if self
            .led_commands
            .try_send(LedCommand::Blink(LedPattern::DoubleBlink))
            .is_err()
        {
            debug!("[Router] LED command queue full (TX blink)");
        }
    }

    async fn handle_long_range_message(&mut self, msg: Message) {
        if self
            .led_commands
            .try_send(LedCommand::Blink(LedPattern::SingleBlink))
            .is_err()
        {
            debug!("[Router] LED command queue full (RX blink)");
        }

        match msg {
            Message::Text(text_msg) => {
                // Extract Copy values for logging and ACK before moving
                let seq = text_msg.seq;
                let has_gps = text_msg.has_gps;

                info!(
                    "[Router] Received TEXT message: seq={}, text='{}', has_gps={}",
                    seq,
                    text_msg.text.as_str(),
                    has_gps
                );

                // Send ACK back via LoRa with jitter to avoid collisions
                let jitter_ms = ACK_JITTER_MIN_MS
                    + (Instant::now().as_ticks() % (ACK_JITTER_MAX_MS - ACK_JITTER_MIN_MS));
                debug!(
                    "[Router] Delaying ACK by {} ms (jitter range: {}-{} ms)",
                    jitter_ms, ACK_JITTER_MIN_MS, ACK_JITTER_MAX_MS
                );
                Timer::after(Duration::from_millis(jitter_ms)).await;

                let ack = Message::Ack(AckMessage { seq });
                info!("[Router] Sending ACK for seq={} via LoRa", seq);
                self.tx_to_lora.send(ack).await;
                debug!("[Router] ACK queued for LoRa TX");

                // Reconstruct message for forwarding (text_msg was moved out of match)
                let msg = Message::Text(text_msg);

                // Forward to BLE or buffer
                if self.connection_state == ConnectionState::Connected {
                    // try_send returns TrySendError::Full(msg) on failure, so we get ownership back
                    match self.tx_to_ble.try_send(msg) {
                        Ok(_) => {
                            info!("[Router] Forwarded TEXT message to BLE (seq={})", seq);
                        }
                        Err(TrySendError::Full(returned_msg)) => {
                            warn!(
                                "[Router] BLE TX channel full, buffering message (seq={})",
                                seq
                            );
                            match self.storage.add(&returned_msg) {
                                Ok(_) => {
                                    info!(
                                        "[Router] Message buffered successfully (storage count: {})",
                                        self.storage.count()
                                    );
                                }
                                Err(e) => {
                                    error!(
                                        "[Router] FAILED to buffer message: {:?} (message may be lost!)",
                                        e
                                    );
                                }
                            }
                        }
                    }
                } else {
                    info!("[Router] BLE disconnected, buffering message (seq={})", seq);
                    match self.storage.add(&msg) {
                        Ok(_) => {
                            info!(
                                "[Router] Message buffered for later delivery (storage count: {})",
                                self.storage.count()
                            );
                        }
                        Err(e) => {
                            error!(
                                "[Router] FAILED to buffer message: {:?} (message may be lost!)",
                                e
                            );
                        }
                    }
                }
            }
            Message::Ack(ack_msg) => {
                let seq = ack_msg.seq;
                info!("[Router] Received ACK message: seq={}", seq);

                if self.connection_state == ConnectionState::Connected {
                    // Reconstruct and move - ACKs aren't buffered, so no need to recover on failure
                    match self.tx_to_ble.try_send(Message::Ack(ack_msg)) {
                        Ok(_) => {
                            info!("[Router] Forwarded ACK to BLE (seq={})", seq);
                        }
                        Err(_) => {
                            warn!(
                                "[Router] Failed to forward ACK to BLE (channel full, seq={})",
                                seq
                            );
                        }
                    }
                } else {
                    debug!(
                        "[Router] Dropping ACK (BLE disconnected, ACKs not buffered, seq={})",
                        seq
                    );
                }
            }
        }
    }
}
