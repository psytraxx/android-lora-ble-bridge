package com.example.lorabridge.data.protocol

import com.example.lorabridge.domain.model.Message
import com.example.lorabridge.domain.model.MessageType
import org.junit.Assert.*
import org.junit.Test

/**
 * Unit tests for LoRa Protocol serialization/deserialization
 */
class LoRaProtocolTest {

    @Test
    fun `serialize and deserialize text message without GPS`() {
        val original = Message.TextMessage(
            seq = 42,
            text = "HELLO WORLD",
            hasGps = false
        )

        val serialized = LoRaProtocol.serialize(original)
        val deserialized = LoRaProtocol.deserialize(serialized) as Message.TextMessage

        assertEquals(original.seq, deserialized.seq)
        assertEquals(original.text, deserialized.text)
        assertEquals(original.hasGps, deserialized.hasGps)
        assertNull(deserialized.latitude)
        assertNull(deserialized.longitude)
    }

    @Test
    fun `serialize and deserialize text message with GPS`() {
        val original = Message.TextMessage(
            seq = 99,
            text = "TEST MESSAGE",
            hasGps = true,
            latitude = 47.123456,
            longitude = 8.987654
        )

        val serialized = LoRaProtocol.serialize(original)
        val deserialized = LoRaProtocol.deserialize(serialized) as Message.TextMessage

        assertEquals(original.seq, deserialized.seq)
        assertEquals(original.text, deserialized.text)
        assertEquals(original.hasGps, deserialized.hasGps)
        assertNotNull(deserialized.latitude)
        assertNotNull(deserialized.longitude)

        // GPS coordinates should be accurate to 6 decimal places (1 meter precision)
        assertEquals(original.latitude!!, deserialized.latitude!!, 0.000001)
        assertEquals(original.longitude!!, deserialized.longitude!!, 0.000001)
    }

    @Test
    fun `serialize and deserialize ACK message`() {
        val original = Message.AckMessage(seq = 123)

        val serialized = LoRaProtocol.serialize(original)
        val deserialized = LoRaProtocol.deserialize(serialized) as Message.AckMessage

        assertEquals(original.seq, deserialized.seq)
        assertEquals(2, serialized.size)  // ACK is 2 bytes
    }

    @Test
    fun `text with max length should serialize correctly`() {
        val maxText = "A".repeat(Message.MAX_TEXT_LENGTH)
        val message = Message.TextMessage(
            seq = 1,
            text = maxText,
            hasGps = false
        )

        val serialized = LoRaProtocol.serialize(message)
        val deserialized = LoRaProtocol.deserialize(serialized) as Message.TextMessage

        assertEquals(maxText, deserialized.text)
    }

    @Test
    fun `lowercase text should be converted to uppercase`() {
        val original = Message.TextMessage(
            seq = 1,
            text = "hello world",
            hasGps = false
        )

        val serialized = LoRaProtocol.serialize(original)
        val deserialized = LoRaProtocol.deserialize(serialized) as Message.TextMessage

        assertEquals("HELLO WORLD", deserialized.text)
    }

    @Test
    fun `special characters should be supported`() {
        val specialChars = ".,!?-:;'\"@#$%&*()[]{}=+/<>_"
        val message = Message.TextMessage(
            seq = 1,
            text = specialChars,
            hasGps = false
        )

        val serialized = LoRaProtocol.serialize(message)
        val deserialized = LoRaProtocol.deserialize(serialized) as Message.TextMessage

        assertEquals(specialChars, deserialized.text)
    }

    @Test
    fun `isTextSupported should validate character set`() {
        assertTrue(LoRaProtocol.isTextSupported("HELLO WORLD"))
        assertTrue(LoRaProtocol.isTextSupported("hello world"))
        assertTrue(LoRaProtocol.isTextSupported("ABC123"))
        assertTrue(LoRaProtocol.isTextSupported(".,!?-"))

        assertFalse(LoRaProtocol.isTextSupported("Hello 世界"))  // Contains unsupported characters
        assertFalse(LoRaProtocol.isTextSupported("Test\nNewline"))  // Contains newline
    }

    @Test
    fun `calculatePackedSize should return correct byte count`() {
        // 50 chars * 6 bits = 300 bits = 37.5 bytes -> 38 bytes
        assertEquals(38, LoRaProtocol.calculatePackedSize("A".repeat(50)))

        // "HELLO WORLD" = 11 chars * 6 bits = 66 bits = 8.25 bytes -> 9 bytes
        assertEquals(9, LoRaProtocol.calculatePackedSize("HELLO WORLD"))

        // 10 chars * 6 bits = 60 bits = 7.5 bytes -> 8 bytes
        assertEquals(8, LoRaProtocol.calculatePackedSize("A".repeat(10)))
    }

    @Test
    fun `message type should be correctly set in serialized data`() {
        val textMsg = Message.TextMessage(seq = 1, text = "TEST", hasGps = false)
        val ackMsg = Message.AckMessage(seq = 1)

        val textSerialized = LoRaProtocol.serialize(textMsg)
        val ackSerialized = LoRaProtocol.serialize(ackMsg)

        assertEquals(MessageType.TEXT.value, textSerialized[0])
        assertEquals(MessageType.ACK.value, ackSerialized[0])
    }

    @Test(expected = IllegalArgumentException::class)
    fun `deserialize should fail with empty data`() {
        LoRaProtocol.deserialize(byteArrayOf())
    }

    @Test(expected = IllegalArgumentException::class)
    fun `deserialize should fail with invalid message type`() {
        LoRaProtocol.deserialize(byteArrayOf(0xFF.toByte(), 0x00))
    }

    @Test
    fun `round trip with all supported characters`() {
        val allChars = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:;'\"@#$%&*()[]{}=+/<>_"
        val message = Message.TextMessage(
            seq = 50,
            text = allChars.take(Message.MAX_TEXT_LENGTH),
            hasGps = false
        )

        val serialized = LoRaProtocol.serialize(message)
        val deserialized = LoRaProtocol.deserialize(serialized) as Message.TextMessage

        assertEquals(message.text, deserialized.text)
    }
}
