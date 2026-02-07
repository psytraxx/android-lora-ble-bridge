package com.example.lorabridge.domain.model

/**
 * Device info from BLE read characteristic (16 bytes)
 * [Battery:1][RSSI:2 LE][SNR:2 LE][TxPower:1][Freq:4 LE][BW:4 LE][SF:1][CR:1]
 */
data class DeviceInfo(
    val batteryLevel: Int,      // 0-100%
    val rssi: Int,              // dBm
    val snr: Float,             // dB
    val txPower: Int,           // dBm
    val frequencyHz: Long,      // Hz
    val bandwidthHz: Long,      // Hz
    val spreadingFactor: Int,   // 7-12
    val codingRate: Int         // 5-8
)
