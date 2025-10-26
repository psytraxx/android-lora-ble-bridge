package com.example.lorabridge.domain.model

/**
 * UI model for displaying messages in chat
 */
data class ChatMessage(
    val text: String,
    val isSent: Boolean,  // true = sent by user, false = received
    val timestamp: Long = System.currentTimeMillis(),
    val seq: Byte,
    val ackStatus: AckStatus = if (isSent) AckStatus.PENDING else AckStatus.NONE,
    val hasGps: Boolean = false,
    val latitude: Double? = null,
    val longitude: Double? = null
) {
    /**
     * Check if message has valid GPS coordinates for Maps click
     */
    fun canOpenMaps(): Boolean = hasGps && latitude != null && longitude != null
}

enum class AckStatus {
    NONE,       // Not applicable (received messages)
    PENDING,    // Sent, waiting for ACK
    DELIVERED   // ACK received
}
