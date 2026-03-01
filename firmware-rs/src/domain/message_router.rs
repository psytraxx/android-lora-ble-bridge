//! Message router - core business logic for routing messages between transports.
//! This is the heart of the channel-based architecture's domain layer.
//! It orchestrates communication between short-range (BLE) and long-range (LoRa) tasks
//! via Embassy channels, completely decoupled from hardware.

use crate::{
    adapters::deep_sleep_adapter::DeepSleepAdapter,
    adapters::nvs_storage_adapter::NvsStorageAdapter,
    constants::{
        ADVERTISING_DURATION_SECS, CCCD_READY_TIMEOUT_MS, LED_HEARTBEAT_INTERVAL_MS,
        LORA_RETRY_TIMEOUT_MS, LORA_TEXT_RETRIES,
    },
    ports::Sleep as _,
    ports::Storage as _,
    protocol::{AckMessage, Message},
    tasks::{LedCommand, LedPattern, RadioMetadata},
};
use embassy_futures::select::{Either, Either4, select, select4};
use embassy_sync::channel::{Receiver, Sender};
use embassy_sync::signal::Signal;
use embassy_sync::{blocking_mutex::raw::CriticalSectionRawMutex, channel::TrySendError};
use embassy_time::{Duration, Instant, Ticker, Timer};
use log::{debug, error, info, warn};

/// A text message pending ACK from the remote side, eligible for retransmission.
struct PendingTx {
    msg: Message,
    seq: u8,
    retries_left: u8,
    deadline: Instant,
}

/// Capacity of the recent-seq ring buffer used for duplicate suppression.
const SEEN_SEQS_CAP: usize = 8;

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
    radio_stats: &'static Signal<CriticalSectionRawMutex, (i16, i8)>,

    // Storage (concrete adapter reference)
    storage: &'static mut NvsStorageAdapter<'static>,

    // Sleep (concrete adapter reference)
    sleep: &'static mut DeepSleepAdapter<'static>,

    // CCCD-ready channel: BLE task sends () when client enables TX notifications.
    // Capacity-1 channel lets try_receive() consume stale values from a previous connection.
    cccd_ready: Receiver<'static, CriticalSectionRawMutex, (), 1>,

    // Retry tracking: text message sent BLE→LoRa awaiting ACK
    pending_tx: Option<PendingTx>,

    // Duplicate suppression for incoming LoRa messages.
    // Stores recent seq numbers in a ring buffer to drop retransmitted duplicates.
    seen_seqs: [u8; SEEN_SEQS_CAP],
    seen_seqs_count: usize, // 0..=SEEN_SEQS_CAP valid entries
    seen_seqs_head: usize,  // next write position
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
        radio_stats: &'static Signal<CriticalSectionRawMutex, (i16, i8)>,
        cccd_ready: Receiver<'static, CriticalSectionRawMutex, (), 1>,
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
            radio_stats,
            cccd_ready,
            storage,
            sleep,
            pending_tx: None,
            seen_seqs: [0u8; SEEN_SEQS_CAP],
            seen_seqs_count: 0,
            seen_seqs_head: 0,
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
                    self.radio_stats.signal((metadata.rssi, metadata.snr));
                    self.handle_long_range_message(msg, false).await;
                }
                Err(_) => {
                    warn!(
                        "[Router] No LoRa packet available despite wake signal (may have been processed)"
                    );
                }
            }
        }

        loop {
            // Advertising phase — runs until connection or timeout.
            // Uses a loop-with-value to make the outcome explicit without a flag variable.
            info!(
                "[Router] Entering BLE advertising phase ({} seconds timeout)...",
                ADVERTISING_DURATION_SECS
            );

            let deadline = Instant::now() + Duration::from_secs(ADVERTISING_DURATION_SECS);
            let mut adv_heartbeat = Ticker::every(Duration::from_millis(LED_HEARTBEAT_INTERVAL_MS));

            let connected = loop {
                match select4(
                    self.connection_state_rx.receive(),
                    self.rx_from_lora.receive(),
                    adv_heartbeat.next(),
                    Timer::at(deadline),
                )
                .await
                {
                    Either4::First(true) => {
                        info!("[Router] BLE connection established!");
                        break true;
                    }
                    Either4::First(false) => {
                        debug!("[Router] Received disconnected state during advertising");
                    }
                    Either4::Second((msg, metadata)) => {
                        info!(
                            "[Router] LoRa message received during advertising: {:?} (RSSI: {})",
                            msg, metadata.rssi
                        );
                        self.signal_activity();
                        self.radio_stats.signal((metadata.rssi, metadata.snr));
                        self.handle_long_range_message(msg, false).await;
                    }
                    Either4::Third(_) => {
                        if self
                            .led_commands
                            .try_send(LedCommand::Blink(LedPattern::Heartbeat))
                            .is_err()
                        {
                            debug!("[Router] LED command queue full (heartbeat)");
                        }
                    }
                    Either4::Fourth(_) => break false, // advertising timeout
                }
            };

            if connected {
                info!("[Router] Starting connected routing loop...");
                self.routing_loop().await;
                warn!("[Router] BLE disconnected, re-entering advertising phase...");
                // Loop back to advertising — no sleep on disconnect
            } else {
                info!(
                    "[Router] Advertising timeout after {} seconds with no connection",
                    ADVERTISING_DURATION_SECS
                );
                info!("[Router] Entering deep sleep (wake sources: LoRa DIO, button)...");
                info!(
                    "[Router] Buffered messages remaining: {}",
                    self.storage.count()
                );
                self.sleep.enter_sleep();
                // Note: Deep sleep resets the CPU, so we never get here
            }
        }
    }

    /// Record an incoming LoRa seq number and return true if it was already seen (duplicate).
    fn check_and_record_seq(&mut self, seq: u8) -> bool {
        // Check all valid entries
        for i in 0..self.seen_seqs_count {
            if self.seen_seqs[i] == seq {
                return true; // duplicate
            }
        }
        // Record: if buffer not full, append; otherwise overwrite oldest (ring)
        self.seen_seqs[self.seen_seqs_head] = seq;
        self.seen_seqs_head = (self.seen_seqs_head + 1) % SEEN_SEQS_CAP;
        if self.seen_seqs_count < SEEN_SEQS_CAP {
            self.seen_seqs_count += 1;
        }
        false
    }

    /// Retransmit the pending message or give up if retries are exhausted.
    async fn handle_retry(&mut self) {
        let Some(pending) = self.pending_tx.take() else {
            return;
        };
        if pending.retries_left > 0 {
            info!(
                "[Router] Retrying seq={} ({} retries left)",
                pending.seq, pending.retries_left
            );
            self.tx_to_lora.send(pending.msg.clone()).await;
            self.pending_tx = Some(PendingTx {
                msg: pending.msg,
                seq: pending.seq,
                retries_left: pending.retries_left - 1,
                deadline: Instant::now() + Duration::from_millis(LORA_RETRY_TIMEOUT_MS),
            });
        } else {
            warn!(
                "[Router] seq={} exhausted all retries, giving up",
                pending.seq
            );
        }
    }

    /// Main routing loop while connected.
    ///
    /// Mirrors the Arduino main loop: poll `areNotificationsEnabled()` every iteration
    /// and send one buffered message when the flag is set.  No pre-loop drain phase,
    /// no fixed delays — just continuous polling like the C++ firmware.
    async fn routing_loop(&mut self) {
        self.signal_activity();
        let mut heartbeat_ticker = Ticker::every(Duration::from_millis(LED_HEARTBEAT_INTERVAL_MS));

        // Clear any stale CCCD value left over from a previous connection.
        let _ = self.cccd_ready.try_receive();
        let mut notifications_enabled = false;
        // Fallback: if CCCD write is never surfaced (trouble-host absorbs it internally),
        // enable drain after this deadline — matching the old 500 ms conservative wait.
        let cccd_deadline = Instant::now() + Duration::from_millis(CCCD_READY_TIMEOUT_MS);

        info!("[Router] Entering connected routing loop...");

        loop {
            // Retry timer: fires at pending_tx.deadline, or never if no pending TX
            let retry_fut = async {
                match self.pending_tx {
                    Some(ref p) => Timer::at(p.deadline).await,
                    None => core::future::pending::<()>().await,
                }
            };

            // Drain tick: when buffered messages are ready, wake every 50 ms so we
            // don't stall waiting 2 s for the heartbeat.
            let has_buffered = notifications_enabled && self.storage.count() > 0;
            let drain_tick = async move {
                if has_buffered {
                    Timer::after_millis(50).await
                } else {
                    core::future::pending::<()>().await
                }
            };

            match select(
                drain_tick,
                select4(
                    self.rx_from_ble.receive(),
                    self.rx_from_lora.receive(),
                    heartbeat_ticker.next(),
                    retry_fut,
                ),
            )
            .await
            {
                Either::First(_) => {} // drain tick — fall through to drain below
                Either::Second(Either4::First(msg)) => {
                    info!("[Router] BLE -> LoRa: {:?}", msg);
                    self.signal_activity();
                    self.handle_short_range_message(msg).await;
                }
                Either::Second(Either4::Second((msg, metadata))) => {
                    info!(
                        "[Router] LoRa -> BLE: {:?} (RSSI: {}, SNR: {})",
                        msg, metadata.rssi, metadata.snr
                    );
                    self.signal_activity();
                    self.radio_stats.signal((metadata.rssi, metadata.snr));
                    self.handle_long_range_message(msg, true).await;
                }
                Either::Second(Either4::Third(_)) => {
                    if self
                        .led_commands
                        .try_send(LedCommand::Blink(LedPattern::Heartbeat))
                        .is_err()
                    {
                        debug!("[Router] LED command queue full (heartbeat)");
                    }
                }
                Either::Second(Either4::Fourth(_)) => {
                    self.handle_retry().await;
                }
            }

            // Arduino equivalent of areNotificationsEnabled(): check channel (non-blocking).
            if !notifications_enabled {
                if self.cccd_ready.try_receive().is_ok() {
                    notifications_enabled = true;
                    info!("[Router] Client enabled TX notifications");
                } else if Instant::now() >= cccd_deadline {
                    notifications_enabled = true;
                    warn!(
                        "[Router] CCCD not seen after {}ms — draining anyway",
                        CCCD_READY_TIMEOUT_MS
                    );
                }
            }

            // Arduino equivalent of "send buffered message if notifications enabled":
            // drain one message per loop iteration.
            if notifications_enabled
                && let Ok(Some(msg)) = self.storage.peek()
                && self.tx_to_ble.try_send(msg).is_ok()
            {
                let _ = self.storage.pop();
                let remaining = self.storage.count();
                if remaining == 0 {
                    info!("[Router] All buffered messages sent");
                } else {
                    debug!("[Router] Sent 1 buffered message ({} remaining)", remaining);
                }
                self.signal_activity();
            }
            // If try_send fails (channel full), we retry next iteration — same as Arduino

            // Check for disconnection
            if let Ok(false) = self.connection_state_rx.try_receive() {
                info!("[Router] BLE disconnection signal received");
                break;
            }
        }

        info!("[Router] Exiting routing loop");
    }

    async fn handle_short_range_message(&mut self, msg: Message) {
        info!("[Router] Forwarding message from BLE to LoRa: {:?}", msg);

        // Extract seq before moving msg
        let seq = if let Message::Text(ref t) = msg {
            Some(t.seq)
        } else {
            None
        };

        self.tx_to_lora.send(msg.clone()).await;
        debug!("[Router] Message queued for LoRa TX");

        // Track retries for text messages (ACKs and future relay msgs use 0 retries)
        if let Some(seq) = seq {
            if self.pending_tx.is_some() {
                warn!(
                    "[Router] Replacing pending retry (seq={}) with new message",
                    seq
                );
            }
            self.pending_tx = Some(PendingTx {
                msg,
                seq,
                retries_left: LORA_TEXT_RETRIES,
                deadline: Instant::now() + Duration::from_millis(LORA_RETRY_TIMEOUT_MS),
            });
            info!(
                "[Router] Tracking retry for seq={} ({} retries, timeout {}ms)",
                seq, LORA_TEXT_RETRIES, LORA_RETRY_TIMEOUT_MS
            );
        }

        if self
            .led_commands
            .try_send(LedCommand::Blink(LedPattern::DoubleBlink))
            .is_err()
        {
            debug!("[Router] LED command queue full (TX blink)");
        }
    }

    async fn handle_long_range_message(&mut self, msg: Message, is_connected: bool) {
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

                // Duplicate suppression: drop messages with a seq we've seen recently
                if self.check_and_record_seq(seq) {
                    info!("[Router] Dropping duplicate TEXT message (seq={})", seq);
                    return;
                }

                // Implicit ACK: if we're retrying a message and we hear the same seq
                // coming back over LoRa (e.g. relayed by another node), cancel retries.
                if let Some(ref pending) = self.pending_tx
                    && pending.seq == seq
                {
                    info!(
                        "[Router] Implicit ACK: heard seq={} on air, cancelling retries",
                        seq
                    );
                    self.pending_tx = None;
                }

                info!(
                    "[Router] Received TEXT message: seq={}, text='{}', has_gps={}",
                    seq,
                    text_msg.text.as_str(),
                    has_gps
                );

                // CAD handles collision avoidance in lora_task, so send ACK immediately
                let ack = Message::Ack(AckMessage { seq });
                info!("[Router] Sending ACK for seq={} via LoRa", seq);
                self.tx_to_lora.send(ack).await;
                debug!("[Router] ACK queued for LoRa TX");

                // Reconstruct message for forwarding (text_msg was moved out of match)
                let msg = Message::Text(text_msg);

                // Forward to BLE or buffer
                if is_connected {
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

                // Cancel pending retry if this ACK matches our outgoing message
                if let Some(ref pending) = self.pending_tx
                    && pending.seq == seq
                {
                    info!(
                        "[Router] ACK confirmed delivery of seq={}, cancelling retries",
                        seq
                    );
                    self.pending_tx = None;
                }

                if is_connected {
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
