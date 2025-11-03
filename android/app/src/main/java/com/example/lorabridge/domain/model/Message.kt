package com.example.lorabridge.domain.model

/**
 * Domain model for LoRa messages
 * Protocol v3.0 - Unified text + GPS
 */
sealed class Message {
    abstract val type: MessageType

    data class TextMessage(
        val seq: Byte,
        val text: String,
        val hasGps: Boolean = false,
        val latitude: Double? = null,  // Only valid if hasGps = true
        val longitude: Double? = null  // Only valid if hasGps = true
    ) : Message() {
        override val type = MessageType.TEXT

        init {
            require(text.length <= MAX_TEXT_LENGTH) {
                "Text too long (max $MAX_TEXT_LENGTH chars)"
            }
            if (hasGps) {
                requireNotNull(latitude) { "Latitude required when hasGps=true" }
                requireNotNull(longitude) { "Longitude required when hasGps=true" }
            }
        }
    }

    data class AckMessage(
        val seq: Byte
    ) : Message() {
        override val type = MessageType.ACK
    }

    /**
     * Wake-up message - LoRa-only, used to wake devices from deep sleep.
     * Never sent via BLE.
     */
    data object WakeUpMessage : Message() {
        override val type = MessageType.WAKE_UP
    }

    companion object {
        const val MAX_TEXT_LENGTH = 50
    }
}

enum class MessageType(val value: Byte) {
    TEXT(0x01),
    ACK(0x02),
    WAKE_UP(0x03);  // Wake-up message (LoRa-only, never sent via BLE)

    companion object {
        fun fromByte(value: Byte): MessageType =
            entries.find { it.value == value }
                ?: throw IllegalArgumentException("Unknown message type: $value")
    }
}
