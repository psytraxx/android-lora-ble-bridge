package com.example.lorabridge.domain.model

import org.junit.Assert.*
import org.junit.Test

/**
 * Unit tests for ChatMessage model
 * @see UC-6.1: Display Chat Message
 */
class ChatMessageTest {

    @Test
    fun `sent message should have PENDING ack status by default`() {
        val message = ChatMessage(
            text = "TEST",
            isSent = true,
            seq = 1
        )

        assertEquals(AckStatus.PENDING, message.ackStatus)
    }

    @Test
    fun `received message should have NONE ack status`() {
        val message = ChatMessage(
            text = "TEST",
            isSent = false,
            seq = 1
        )

        assertEquals(AckStatus.NONE, message.ackStatus)
    }

    @Test
    fun `canOpenMaps should return true when has GPS coordinates`() {
        val message = ChatMessage(
            text = "TEST",
            isSent = true,
            seq = 1,
            hasGps = true,
            latitude = 47.123456,
            longitude = 8.987654
        )

        assertTrue(message.canOpenMaps())
    }

    @Test
    fun `canOpenMaps should return false when no GPS`() {
        val message = ChatMessage(
            text = "TEST",
            isSent = true,
            seq = 1,
            hasGps = false
        )

        assertFalse(message.canOpenMaps())
    }

    @Test
    fun `canOpenMaps should return false when GPS flag true but coordinates null`() {
        val message = ChatMessage(
            text = "TEST",
            isSent = true,
            seq = 1,
            hasGps = true,
            latitude = null,
            longitude = null
        )

        assertFalse(message.canOpenMaps())
    }

    @Test
    fun `canOpenMaps should return false when latitude is null`() {
        val message = ChatMessage(
            text = "TEST",
            isSent = true,
            seq = 1,
            hasGps = true,
            latitude = null,
            longitude = 8.987654
        )

        assertFalse(message.canOpenMaps())
    }

    @Test
    fun `canOpenMaps should return false when longitude is null`() {
        val message = ChatMessage(
            text = "TEST",
            isSent = true,
            seq = 1,
            hasGps = true,
            latitude = 47.123456,
            longitude = null
        )

        assertFalse(message.canOpenMaps())
    }

    @Test
    fun `timestamp should be set automatically`() {
        val before = System.currentTimeMillis()
        val message = ChatMessage(
            text = "TEST",
            isSent = true,
            seq = 1
        )
        val after = System.currentTimeMillis()

        assertTrue(message.timestamp >= before)
        assertTrue(message.timestamp <= after)
    }

    @Test
    fun `copy should preserve all fields`() {
        val original = ChatMessage(
            text = "ORIGINAL",
            isSent = true,
            seq = 42,
            ackStatus = AckStatus.PENDING,
            hasGps = true,
            latitude = 47.0,
            longitude = 8.0
        )

        val copied = original.copy()

        assertEquals(original.text, copied.text)
        assertEquals(original.isSent, copied.isSent)
        assertEquals(original.seq, copied.seq)
        assertEquals(original.ackStatus, copied.ackStatus)
        assertEquals(original.hasGps, copied.hasGps)
        assertEquals(original.latitude, copied.latitude)
        assertEquals(original.longitude, copied.longitude)
        assertEquals(original.timestamp, copied.timestamp)
    }

    @Test
    fun `copy with ackStatus change should work`() {
        val original = ChatMessage(
            text = "TEST",
            isSent = true,
            seq = 1,
            ackStatus = AckStatus.PENDING
        )

        val updated = original.copy(ackStatus = AckStatus.DELIVERED)

        assertEquals(AckStatus.PENDING, original.ackStatus)
        assertEquals(AckStatus.DELIVERED, updated.ackStatus)
        assertEquals(original.text, updated.text)
        assertEquals(original.seq, updated.seq)
    }
}
