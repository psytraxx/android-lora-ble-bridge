package com.example.lorabridge.data.repository

import com.example.lorabridge.domain.model.AckStatus
import com.example.lorabridge.domain.model.ChatMessage
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Repository for managing chat messages
 * Could be extended to persist messages to local database
 */
@Singleton
class MessageRepository @Inject constructor() {

    private val _messages = MutableStateFlow<List<ChatMessage>>(emptyList())
    val messages: StateFlow<List<ChatMessage>> = _messages.asStateFlow()

    /**
     * Add a new message to the chat
     * @see UC-6.1: Display Chat Message
     */
    fun addMessage(message: ChatMessage) {
        _messages.value += message
    }

    /**
     * Update ACK status for a sent message
     * @see UC-6.2: Update ACK Status Indicator
     */
    fun updateAckStatus(seq: Byte, status: AckStatus) {
        _messages.value = _messages.value.map { msg ->
            if (msg.isSent && msg.seq == seq) {
                msg.copy(ackStatus = status)
            } else {
                msg
            }
        }
    }

    /**
     * Clear all messages
     */
    fun clearMessages() {
        _messages.value = emptyList()
    }

    /**
     * Get message by sequence number
     */
    fun findMessageBySeq(seq: Byte): ChatMessage? {
        return _messages.value.find { it.seq == seq }
    }
}
