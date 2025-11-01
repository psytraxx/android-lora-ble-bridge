package com.example.lorabridge.data.repository

import com.example.lorabridge.domain.model.AckStatus
import com.example.lorabridge.domain.model.ChatMessage
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * Unit tests for MessageRepository
 * @see UC-6.1: Display Chat Message
 * @see UC-6.2: Update ACK Status Indicator
 */
class MessageRepositoryTest {

    private lateinit var repository: MessageRepository

    @Before
    fun setup() {
        repository = MessageRepository()
    }

    @Test
    fun `initial messages should be empty`() = runTest {
        val messages = repository.messages.first()
        assertTrue(messages.isEmpty())
    }

    @Test
    fun `addMessage should add message to list`() = runTest {
        val message = ChatMessage(
            text = "TEST",
            isSent = true,
            seq = 1
        )

        repository.addMessage(message)

        val messages = repository.messages.first()
        assertEquals(1, messages.size)
        assertEquals(message, messages[0])
    }

    @Test
    fun `addMessage should append to existing messages`() = runTest {
        val message1 = ChatMessage(text = "FIRST", isSent = true, seq = 1)
        val message2 = ChatMessage(text = "SECOND", isSent = false, seq = 2)

        repository.addMessage(message1)
        repository.addMessage(message2)

        val messages = repository.messages.first()
        assertEquals(2, messages.size)
        assertEquals(message1, messages[0])
        assertEquals(message2, messages[1])
    }

    @Test
    fun `updateAckStatus should update correct message`() = runTest {
        val message1 = ChatMessage(text = "FIRST", isSent = true, seq = 1)
        val message2 = ChatMessage(text = "SECOND", isSent = true, seq = 2)

        repository.addMessage(message1)
        repository.addMessage(message2)

        repository.updateAckStatus(seq = 1, status = AckStatus.DELIVERED)

        val messages = repository.messages.first()
        assertEquals(AckStatus.DELIVERED, messages[0].ackStatus)
        assertEquals(AckStatus.PENDING, messages[1].ackStatus) // Should remain unchanged
    }

    @Test
    fun `updateAckStatus should only update sent messages`() = runTest {
        val sentMessage = ChatMessage(text = "SENT", isSent = true, seq = 1)
        val receivedMessage = ChatMessage(text = "RECEIVED", isSent = false, seq = 1)

        repository.addMessage(sentMessage)
        repository.addMessage(receivedMessage)

        repository.updateAckStatus(seq = 1, status = AckStatus.DELIVERED)

        val messages = repository.messages.first()
        assertEquals(AckStatus.DELIVERED, messages[0].ackStatus) // Sent should update
        assertEquals(AckStatus.NONE, messages[1].ackStatus) // Received should not change
    }

    @Test
    fun `updateAckStatus with non-existent seq should not crash`() = runTest {
        val message = ChatMessage(text = "TEST", isSent = true, seq = 1)
        repository.addMessage(message)

        // Should not crash
        repository.updateAckStatus(seq = 99, status = AckStatus.DELIVERED)

        val messages = repository.messages.first()
        assertEquals(AckStatus.PENDING, messages[0].ackStatus) // Should remain unchanged
    }

    @Test
    fun `clearMessages should remove all messages`() = runTest {
        repository.addMessage(ChatMessage(text = "TEST1", isSent = true, seq = 1))
        repository.addMessage(ChatMessage(text = "TEST2", isSent = true, seq = 2))

        repository.clearMessages()

        val messages = repository.messages.first()
        assertTrue(messages.isEmpty())
    }

    @Test
    fun `findMessageBySeq should find correct message`() = runTest {
        val message1 = ChatMessage(text = "FIRST", isSent = true, seq = 1)
        val message2 = ChatMessage(text = "SECOND", isSent = true, seq = 2)

        repository.addMessage(message1)
        repository.addMessage(message2)

        val found = repository.findMessageBySeq(2)

        assertNotNull(found)
        assertEquals(message2.text, found?.text)
        assertEquals(2.toByte(), found?.seq)
    }

    @Test
    fun `findMessageBySeq should return null for non-existent seq`() = runTest {
        repository.addMessage(ChatMessage(text = "TEST", isSent = true, seq = 1))

        val found = repository.findMessageBySeq(99)

        assertNull(found)
    }

    @Test
    fun `findMessageBySeq should find first match when multiple with same seq`() = runTest {
        // Edge case: same seq on sent and received (shouldn't happen, but test it)
        val message1 = ChatMessage(text = "FIRST", isSent = true, seq = 1)
        val message2 = ChatMessage(text = "SECOND", isSent = false, seq = 1)

        repository.addMessage(message1)
        repository.addMessage(message2)

        val found = repository.findMessageBySeq(1)

        assertNotNull(found)
        assertEquals("FIRST", found?.text)
    }

    @Test
    fun `messages should maintain order`() = runTest {
        val messages = listOf(
            ChatMessage(text = "MSG1", isSent = true, seq = 1),
            ChatMessage(text = "MSG2", isSent = false, seq = 2),
            ChatMessage(text = "MSG3", isSent = true, seq = 3),
            ChatMessage(text = "MSG4", isSent = false, seq = 4)
        )

        messages.forEach { repository.addMessage(it) }

        val retrieved = repository.messages.first()
        assertEquals(4, retrieved.size)
        assertEquals("MSG1", retrieved[0].text)
        assertEquals("MSG2", retrieved[1].text)
        assertEquals("MSG3", retrieved[2].text)
        assertEquals("MSG4", retrieved[3].text)
    }
}
