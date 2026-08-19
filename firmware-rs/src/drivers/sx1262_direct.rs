//! Direct SX1262 SPI commands for reading buffered packets on wake.
//!
//! This module provides low-level SPI access to the SX1262 radio to read packets
//! that are already in the FIFO buffer when waking from deep sleep. This is needed
//! because lora-phy's `rx().await` starts a NEW receive session, which would miss
//! the packet that triggered the wake interrupt (DIO1 going HIGH).
//!
//! This code runs BEFORE lora-phy initialization to preserve the buffered packet.

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_time::Timer;
use embedded_hal::digital::OutputPin;
use embedded_hal_async::digital::Wait;
use embedded_hal_async::spi::SpiBus;
use log::{info, warn};

/// SX1262 SPI opcodes
const OPCODE_GET_STATUS: u8 = 0xC0;
const OPCODE_GET_RX_BUFFER_STATUS: u8 = 0x13;
const OPCODE_READ_BUFFER: u8 = 0x1E;
const OPCODE_GET_PACKET_STATUS: u8 = 0x14;
const OPCODE_GET_IRQ_STATUS: u8 = 0x12;

/// Error type for direct SX1262 operations
#[derive(Debug)]
pub enum Sx1262Error {
    /// SPI communication error
    Spi,
    /// Timeout waiting for BUSY pin
    Busy,
}

/// Read a packet from SX1262 buffer after wake from deep sleep.
///
/// This reads the packet that triggered the DIO1 interrupt without
/// starting a new RX session. The packet data remains in the SX1262's
/// FIFO buffer until explicitly read.
///
/// # Arguments
/// * `spi_bus` - Mutex-protected SPI bus
/// * `cs` - Chip select pin (active low)
/// * `busy` - BUSY pin for synchronization
/// * `buffer` - Buffer to store received packet data
///
/// # Returns
/// * `Ok(Some((len, rssi, snr)))` - Packet read successfully
/// * `Ok(None)` - No packet in buffer
/// * `Err(_)` - Communication error
pub async fn read_wake_packet<SPI, CS, BUSY>(
    spi_bus: &Mutex<CriticalSectionRawMutex, SPI>,
    cs: &mut CS,
    busy: &mut BUSY,
    buffer: &mut [u8],
) -> Result<Option<(u8, i16, i8)>, Sx1262Error>
where
    SPI: SpiBus,
    CS: OutputPin,
    BUSY: Wait,
{
    // Wait for chip to be ready after wake
    busy.wait_for_low().await.map_err(|_| Sx1262Error::Busy)?;

    // Small delay to ensure SPI is stable after ESP32 deep sleep wake
    Timer::after_micros(100).await;

    // 0. GetStatus (0xC0) - "wake up" the SPI interface and check chip state
    // This also helps synchronize the SPI after ESP32 deep sleep
    let mut status_buf = [OPCODE_GET_STATUS, 0x00];
    {
        let mut spi = spi_bus.lock().await;
        cs.set_low().map_err(|_| Sx1262Error::Spi)?;
        spi.transfer_in_place(&mut status_buf)
            .await
            .map_err(|_| Sx1262Error::Spi)?;
        cs.set_high().map_err(|_| Sx1262Error::Spi)?;
    }

    let chip_status = status_buf[1];
    let chip_mode = (chip_status >> 4) & 0x07;
    let cmd_status = (chip_status >> 1) & 0x07;
    info!(
        "[SX1262-Direct] Chip status: 0x{:02X} (mode={}, cmd={})",
        chip_status, chip_mode, cmd_status
    );

    // If chip is still in RX mode (5), wait for packet reception to complete.
    // After RxDone, chip transitions to STDBY (mode 2 or 3).
    // SF11/BW125: preamble=64sym × 16.384ms = 1048ms + body ≈ 2.5–3.5s total.
    // Wait up to 4000ms (400 × 10ms) for reception to finish.
    if chip_mode == 5 {
        info!("[SX1262-Direct] Chip still in RX mode, waiting for reception to complete...");
        let mut rx_done = false;
        for i in 0..400 {
            Timer::after_millis(10).await;

            // Poll GetIrqStatus — fastest way to detect RxDone without mode change.
            // Response: [stale_status, status, IRQ_MSB, IRQ_LSB]
            //   IRQ_LSB bit 1 = RxDone, IRQ_MSB bit 1 = Timeout (bit 9 of 16-bit word)
            let mut irq_buf = [OPCODE_GET_IRQ_STATUS, 0x00, 0x00, 0x00];
            busy.wait_for_low().await.map_err(|_| Sx1262Error::Busy)?;
            {
                let mut spi = spi_bus.lock().await;
                cs.set_low().map_err(|_| Sx1262Error::Spi)?;
                spi.transfer_in_place(&mut irq_buf)
                    .await
                    .map_err(|_| Sx1262Error::Spi)?;
                cs.set_high().map_err(|_| Sx1262Error::Spi)?;
            }
            let irq_lsb = irq_buf[3];
            let irq_msb = irq_buf[2];

            if irq_lsb & 0x02 != 0 {
                // RxDone — packet is fully in the buffer
                info!(
                    "[SX1262-Direct] RxDone IRQ detected after {}ms",
                    (i + 1) * 10
                );
                rx_done = true;
                break;
            }
            if irq_msb & 0x02 != 0 {
                // Timeout — radio gave up, no packet
                warn!("[SX1262-Direct] RX Timeout IRQ after {}ms", (i + 1) * 10);
                return Ok(None);
            }

            // Fallback: also check chip mode via GetStatus
            let mut status_buf = [OPCODE_GET_STATUS, 0x00];
            busy.wait_for_low().await.map_err(|_| Sx1262Error::Busy)?;
            {
                let mut spi = spi_bus.lock().await;
                cs.set_low().map_err(|_| Sx1262Error::Spi)?;
                spi.transfer_in_place(&mut status_buf)
                    .await
                    .map_err(|_| Sx1262Error::Spi)?;
                cs.set_high().map_err(|_| Sx1262Error::Spi)?;
            }
            let new_mode = (status_buf[1] >> 4) & 0x07;
            if new_mode != 5 {
                info!(
                    "[SX1262-Direct] Chip left RX mode (mode={}) after {}ms",
                    new_mode,
                    (i + 1) * 10
                );
                rx_done = true;
                break;
            }
        }
        if !rx_done {
            warn!("[SX1262-Direct] Timed out after 4000ms — proceeding to buffer read anyway");
        }
    }

    busy.wait_for_low().await.map_err(|_| Sx1262Error::Busy)?;

    // 1. GetRxBufferStatus (0x13)
    // SX1262 SPI protocol: opcode, then NOP to get status, then data bytes
    // Send: [opcode, NOP, NOP, NOP]
    // Recv: [status_stale, status_updated, PayloadLengthRx, RxStartBufferPointer]
    let mut rx_status_buf = [OPCODE_GET_RX_BUFFER_STATUS, 0x00, 0x00, 0x00];
    {
        let mut spi = spi_bus.lock().await;
        cs.set_low().map_err(|_| Sx1262Error::Spi)?;
        spi.transfer_in_place(&mut rx_status_buf)
            .await
            .map_err(|_| Sx1262Error::Spi)?;
        cs.set_high().map_err(|_| Sx1262Error::Spi)?;
    }

    // Response: [stale_status, updated_status, PayloadLengthRx, RxStartBufferPointer]
    let status = rx_status_buf[1]; // Updated status after command processed
    let payload_len = rx_status_buf[2];
    let buffer_offset = rx_status_buf[3];

    info!(
        "[SX1262-Direct] RxBufferStatus: status=0x{:02X}, len={}, offset={}",
        status, payload_len, buffer_offset
    );

    // Validate payload length - 0 means no packet
    if payload_len == 0 {
        info!("[SX1262-Direct] No packet in buffer (len=0)");
        return Ok(None);
    }

    if payload_len as usize > buffer.len() {
        warn!(
            "[SX1262-Direct] Packet too large for buffer: {} > {}",
            payload_len,
            buffer.len()
        );
        return Ok(None);
    }

    // 2. ReadBuffer (0x1E, offset, NOP) then read payload bytes
    // Protocol: [opcode, offset, NOP] gets past command + status, then read data
    // RadioLib sends: cmd bytes, then 1 NOP for status, then NOPs for data
    busy.wait_for_low().await.map_err(|_| Sx1262Error::Busy)?;

    {
        let mut spi = spi_bus.lock().await;
        cs.set_low().map_err(|_| Sx1262Error::Spi)?;

        // Send command header: opcode + offset + NOP (3 bytes to get past status)
        let mut header = [OPCODE_READ_BUFFER, buffer_offset, 0x00];
        spi.transfer_in_place(&mut header)
            .await
            .map_err(|_| Sx1262Error::Spi)?;

        // Read payload data (clock out zeros while reading)
        spi.read(&mut buffer[..payload_len as usize])
            .await
            .map_err(|_| Sx1262Error::Spi)?;

        cs.set_high().map_err(|_| Sx1262Error::Spi)?;
    }

    info!(
        "[SX1262-Direct] Read {} bytes: {:02X?}",
        payload_len,
        &buffer[..payload_len as usize]
    );

    // 3. GetPacketStatus (0x14) -> [stale, status, RssiPkt, SnrPkt, SignalRssiPkt]
    // Send: [opcode, NOP, NOP, NOP, NOP] = 5 bytes
    // Recv: [stale_status, updated_status, RssiPkt, SnrPkt, SignalRssiPkt]
    let mut pkt_status_buf = [OPCODE_GET_PACKET_STATUS, 0x00, 0x00, 0x00, 0x00];

    busy.wait_for_low().await.map_err(|_| Sx1262Error::Busy)?;

    {
        let mut spi = spi_bus.lock().await;
        cs.set_low().map_err(|_| Sx1262Error::Spi)?;
        spi.transfer_in_place(&mut pkt_status_buf)
            .await
            .map_err(|_| Sx1262Error::Spi)?;
        cs.set_high().map_err(|_| Sx1262Error::Spi)?;
    }

    // Response: [stale, status, RssiPkt, SnrPkt, SignalRssiPkt]
    // RSSI = -RssiPkt/2 (value is in half-dB steps, unsigned)
    // SNR = SnrPkt/4 (value is in quarter-dB steps, signed)
    let rssi_raw = pkt_status_buf[2];
    let snr_raw = pkt_status_buf[3] as i8; // Treat as signed

    let rssi: i16 = -(rssi_raw as i16) / 2;
    let snr: i8 = snr_raw / 4;

    info!(
        "[SX1262-Direct] PacketStatus: RSSI={} dBm, SNR={} dB",
        rssi, snr
    );

    Ok(Some((payload_len, rssi, snr)))
}
