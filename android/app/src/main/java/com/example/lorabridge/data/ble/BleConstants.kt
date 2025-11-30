package com.example.lorabridge.data.ble

import java.util.UUID

/**
 * BLE configuration constants for ESP32S3-LoRa device
 */
object BleConstants {
    val SERVICE_UUID: UUID = UUID.fromString("00001234-0000-1000-8000-00805F9B34FB")
    val TX_CHAR_UUID: UUID =
        UUID.fromString("00005678-0000-1000-8000-00805F9B34FB")  // RX from Android perspective (notifications)
    val RX_CHAR_UUID: UUID =
        UUID.fromString("00005679-0000-1000-8000-00805F9B34FB")  // TX from Android perspective (writes)
    val CCCD_UUID: UUID =
        UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")     // Client Characteristic Configuration Descriptor

    // Standard BLE Battery Service
    val BATTERY_SERVICE_UUID: UUID = UUID.fromString("0000180F-0000-1000-8000-00805F9B34FB")
    val BATTERY_LEVEL_UUID: UUID = UUID.fromString("00002A19-0000-1000-8000-00805F9B34FB")

    const val MTU_SIZE = 512
    const val SCAN_TIMEOUT_MS = 30_000L  // 30 seconds
    const val CONNECTION_TIMEOUT_MS = 30_000L  // 30 seconds - increased from 10s for reliability
    const val AUTO_DISCONNECT_DELAY_MS = 60_000L  // 60 seconds inactivity
    const val ACK_TIMEOUT_MS = 5_000L  // 5 seconds waiting for ACK
    const val DEVICE_STALE_TIMEOUT_MS = 15_000L  // 15 seconds - remove devices not seen
}
