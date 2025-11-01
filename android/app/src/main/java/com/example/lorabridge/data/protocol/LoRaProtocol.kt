package com.example.lorabridge.data.protocol

import com.example.lorabridge.domain.model.Message
import com.example.lorabridge.domain.model.MessageType
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * LoRa Message Protocol v3.0
 * Binary serialization with 6-bit character encoding
 *
 * Character set: 64 chars (Space + A-Z + 0-9 + punctuation)
 * Encoding: 6 bits per character
 * Max text: 50 chars = 38 bytes packed
 */
object LoRaProtocol {

    private const val CHARSET = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:;'\"@#$%&*()[]{}=+/<>_"

    /**
     * Serialize a message to bytes for BLE/LoRa transmission
     * @see UC-5.1: Serialize Text Message
     */
    fun serialize(message: Message): ByteArray {
        return when (message) {
            is Message.TextMessage -> serializeTextMessage(message)
            is Message.AckMessage -> serializeAckMessage(message)
        }
    }

    /**
     * Deserialize bytes from BLE/LoRa to Message object
     * @see UC-5.2: Deserialize Received Message
     */
    fun deserialize(data: ByteArray): Message {
        require(data.isNotEmpty()) { "Data too short" }

        val type = MessageType.fromByte(data[0])
        return when (type) {
            MessageType.TEXT -> deserializeTextMessage(data)
            MessageType.ACK -> deserializeAckMessage(data)
        }
    }

    /**
     * Validate if text contains only supported characters
     * @see UC-5.3: Validate Character Support
     */
    fun isTextSupported(text: String): Boolean {
        return text.all { isCharacterSupported(it) }
    }

    /**
     * Validate if a character is supported
     */
    fun isCharacterSupported(ch: Char): Boolean {
        return CHARSET.indexOf(ch.uppercaseChar()) >= 0
    }

    /**
     * Calculate packed size for text
     */
    fun calculatePackedSize(text: String): Int {
        return (text.length * 6 + 7) / 8
    }

    // ========== Private Implementation ==========

    private fun serializeTextMessage(msg: Message.TextMessage): ByteArray {
        val packedText = packText(msg.text)
        val totalSize = 5 + packedText.size + if (msg.hasGps) 8 else 0

        val buffer = ByteBuffer.allocate(totalSize).order(ByteOrder.LITTLE_ENDIAN)

        // Header
        buffer.put(MessageType.TEXT.value)       // [0] Type
        buffer.put(msg.seq)                       // [1] Seq
        buffer.put(msg.text.length.toByte())     // [2] Char count
        buffer.put(packedText.size.toByte())     // [3] Packed length

        // Packed text
        buffer.put(packedText)                    // [4..N] Packed bytes

        // GPS flag and coordinates
        buffer.put(if (msg.hasGps) 1 else 0)     // [N+1] hasGPS

        if (msg.hasGps) {
            val lat = (msg.latitude!! * 1_000_000).toInt()
            val lon = (msg.longitude!! * 1_000_000).toInt()
            buffer.putInt(lat)                    // [N+2..N+5] Latitude
            buffer.putInt(lon)                    // [N+6..N+9] Longitude
        }

        return buffer.array()
    }

    private fun serializeAckMessage(msg: Message.AckMessage): ByteArray {
        return byteArrayOf(MessageType.ACK.value, msg.seq)
    }

    private fun deserializeTextMessage(data: ByteArray): Message.TextMessage {
        require(data.size >= 5) { "Data too short for TextMessage header" }

        val buffer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)

        buffer.get() // Skip type
        val seq = buffer.get()
        val charCount = buffer.get().toInt() and 0xFF
        val packedLen = buffer.get().toInt() and 0xFF

        require(data.size >= 5 + packedLen) { "Data too short for packed text + GPS flag" }

        val packedBytes = ByteArray(packedLen)
        buffer.get(packedBytes)

        val text = unpackText(packedBytes, charCount)
        val hasGps = buffer.get() != 0.toByte()

        return if (hasGps) {
            require(data.size >= 5 + packedLen + 8) { "Data too short for GPS data" }
            val lat = buffer.int / 1_000_000.0
            val lon = buffer.int / 1_000_000.0
            Message.TextMessage(seq, text, hasGps = true, latitude = lat, longitude = lon)
        } else {
            Message.TextMessage(seq, text, hasGps = false)
        }
    }

    private fun deserializeAckMessage(data: ByteArray): Message.AckMessage {
        require(data.size >= 2) { "Data too short for AckMessage" }
        return Message.AckMessage(data[1])
    }

    /**
     * Pack text using 6-bit encoding
     * Each character -> 6 bits (instead of 8 bits UTF-8)
     */
    private fun packText(text: String): ByteArray {
        val charCount = text.length
        val byteCount = (charCount * 6 + 7) / 8
        val result = ByteArray(byteCount)

        var bitOffset = 0

        for (char in text) {
            val value = charTo6Bit(char)
            val byteIdx = bitOffset / 8
            val bitInByte = bitOffset % 8

            if (bitInByte <= 2) {
                // The 6 bits fit within current byte
                result[byteIdx] =
                    (result[byteIdx].toInt() or (value.toInt() shl (2 - bitInByte))).toByte()
            } else {
                // The 6 bits span two bytes
                val bitsInFirst = 8 - bitInByte
                val bitsInSecond = 6 - bitsInFirst

                result[byteIdx] =
                    (result[byteIdx].toInt() or (value.toInt() shr bitsInSecond)).toByte()
                if (byteIdx + 1 < result.size) {
                    result[byteIdx + 1] = (value.toInt() shl (8 - bitsInSecond)).toByte()
                }
            }

            bitOffset += 6
        }

        return result
    }

    /**
     * Unpack 6-bit encoded bytes back to text
     */
    private fun unpackText(packed: ByteArray, charCount: Int): String {
        val result = StringBuilder(charCount)
        var bitOffset = 0

        repeat(charCount) {
            val byteIdx = bitOffset / 8
            val bitInByte = bitOffset % 8

            require(byteIdx < packed.size) { "Insufficient packed data" }

            val value: Byte = if (bitInByte <= 2) {
                // The 6 bits are within current byte
                ((packed[byteIdx].toInt() and 0xFF) ushr (2 - bitInByte) and 0x3F).toByte()
            } else {
                // The 6 bits span two bytes
                val bitsInFirst = 8 - bitInByte
                val bitsInSecond = 6 - bitsInFirst

                val firstPart = packed[byteIdx].toInt() and ((1 shl bitsInFirst) - 1)
                val secondPart = if (byteIdx + 1 < packed.size) {
                    (packed[byteIdx + 1].toInt() and 0xFF) ushr (8 - bitsInSecond)
                } else {
                    throw IllegalArgumentException("Insufficient packed data")
                }

                ((firstPart shl bitsInSecond) or secondPart).toByte()
            }

            result.append(sixBitToChar(value))
            bitOffset += 6
        }

        return result.toString()
    }

    /**
     * Convert character to 6-bit value (auto-convert lowercase to uppercase)
     */
    private fun charTo6Bit(ch: Char): Byte {
        val upperCh = ch.uppercaseChar()
        val index = CHARSET.indexOf(upperCh)
        require(index >= 0) { "Character not supported: '$ch'" }
        return index.toByte()
    }

    /**
     * Convert 6-bit value to character
     */
    private fun sixBitToChar(value: Byte): Char {
        val index = value.toInt() and 0xFF
        require(index in CHARSET.indices) { "Invalid 6-bit value: $value" }
        return CHARSET[index]
    }
}
