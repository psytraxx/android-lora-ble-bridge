package com.example.lorabridge.presentation.chat

import android.util.Log
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.example.lorabridge.data.ble.BleConstants
import com.example.lorabridge.data.ble.BleRepository
import com.example.lorabridge.data.location.LocationRepository
import com.example.lorabridge.data.protocol.LoRaProtocol
import com.example.lorabridge.data.repository.MessageRepository
import com.example.lorabridge.domain.model.AckStatus
import com.example.lorabridge.domain.model.BleConnectionState
import com.example.lorabridge.domain.model.ChatMessage
import com.example.lorabridge.domain.model.Message
import com.example.lorabridge.domain.model.toDisplayString
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

/**
 * ViewModel for chat screen
 * Orchestrates BLE, GPS, and message operations
 */
@HiltViewModel
class ChatViewModel @Inject constructor(
    private val bleRepository: BleRepository,
    private val locationRepository: LocationRepository,
    private val messageRepository: MessageRepository
) : ViewModel() {

    // State
    private val _uiState = MutableStateFlow(ChatUiState())
    val uiState: StateFlow<ChatUiState> = _uiState.asStateFlow()

    private val _toastMessage = MutableSharedFlow<String>(extraBufferCapacity = 1)
    val toastMessage = _toastMessage.asSharedFlow()

    // Sequence counter for messages
    private var seqCounter: Byte = 0
    private var pendingAckSeq: Byte? = null
    private var ackTimeoutJob: Job? = null

    // Pending message for reconnection (UC-1.3)
    private var pendingMessageText: String? = null

    companion object {
        private const val TAG = "ChatViewModel"
    }

    init {
        observeBleConnection()
        observeReceivedMessages()
        observeLocation()
        observeChatMessages()
        observeDiscoveredDevices()
        observeBatteryLevel()
    }

    /**
     * Observe BLE connection state
     */
    private fun observeBleConnection() {
        viewModelScope.launch {
            bleRepository.connectionState.collect { connectionState ->
                // UC-1.3: Check if we have a pending message and just connected
                if (connectionState is BleConnectionState.Connected && pendingMessageText != null) {
                    val messageToSend = pendingMessageText!!
                    pendingMessageText = null
                    Log.d(TAG, "Connection established - sending queued message")
                    _toastMessage.tryEmit("Connected! Sending message...")
                    sendMessageInternal(messageToSend)
                }

                // Allow sending when connected OR disconnected (for queue & reconnect)
                // Only disable when waiting for ACK
                val canSend = pendingAckSeq == null

                _uiState.value = _uiState.value.copy(
                    connectionState = connectionState,
                    connectionStatusText = connectionState.toDisplayString(),
                    canSendMessage = canSend
                )
            }
        }
    }

    /**
     * Observe received messages from BLE
     */
    private fun observeReceivedMessages() {
        viewModelScope.launch {
            bleRepository.receivedMessages.collect { message ->
                handleReceivedMessage(message)
            }
        }
    }

    /**
     * Observe location updates
     */
    private fun observeLocation() {
        viewModelScope.launch {
            locationRepository.currentLocation.collect { location ->
                _uiState.value = _uiState.value.copy(
                    gpsText = location?.toDisplayString() ?: "No GPS fix"
                )
            }
        }
    }

    /**
     * Observe chat messages
     */
    private fun observeChatMessages() {
        viewModelScope.launch {
            messageRepository.messages.collect { messages ->
                _uiState.value = _uiState.value.copy(messages = messages)
            }
        }
    }

    /**
     * Observe discovered BLE devices
     */
    private fun observeDiscoveredDevices() {
        viewModelScope.launch {
            bleRepository.discoveredDevices.collect { devices ->
                _uiState.value = _uiState.value.copy(discoveredDevices = devices)
            }
        }
    }

    /**
     * Observe battery level from ESP32
     */
    private fun observeBatteryLevel() {
        viewModelScope.launch {
            bleRepository.batteryLevel.collect { batteryLevel ->
                _uiState.value = _uiState.value.copy(batteryLevel = batteryLevel)
            }
        }
    }

    /**
     * Start BLE scan
     */
    fun startBleScan() {
        bleRepository.startScan()
    }

    /**
     * Connect to a specific device
     */
    fun connectToDevice(deviceAddress: String) {
        bleRepository.connectToDevice(deviceAddress)
    }

    /**
     * Disconnect BLE (scanning will restart automatically via dialog)
     */
    fun disconnect() {
        bleRepository.disconnect()
        messageRepository.clearMessages()
        // Dialog will automatically start scanning when it appears
    }

    /**
     * Update GPS location
     * @see UC-2.3: Display GPS Coordinates
     */
    fun updateGps() {
        viewModelScope.launch {
            val location = locationRepository.getLastKnownLocation()
            _uiState.value = _uiState.value.copy(
                gpsText = location?.toDisplayString() ?: "No GPS fix"
            )
        }
    }

    /**
     * Send a text message with optional GPS
     * UC-1.3: If disconnected, queue message and reconnect
     */
    fun sendMessage(text: String) {
        if (text.isBlank()) {
            Log.w(TAG, "Cannot send empty message")
            return
        }

        // Validate text length
        if (text.length > Message.MAX_TEXT_LENGTH) {
            _toastMessage.tryEmit("Message too long (max ${Message.MAX_TEXT_LENGTH} chars)")
            return
        }

        // Validate characters
        if (!LoRaProtocol.isTextSupported(text)) {
            _toastMessage.tryEmit("Message contains unsupported characters")
            return
        }

        // Check if connected
        if (!bleRepository.isConnected()) {
            // UC-1.3: Queue message and reconnect
            Log.d(TAG, "Not connected - queueing message and reconnecting")
            pendingMessageText = text
            _toastMessage.tryEmit("Reconnecting...")
            bleRepository.startScan()
            return
        }

        // Connected - send immediately
        sendMessageInternal(text)
    }

    /**
     * Internal send message function (after validation and connection check)
     * @see UC-3.1: Send Text Message with GPS
     */
    private fun sendMessageInternal(text: String) {

        viewModelScope.launch {
            // Request fresh GPS update
            locationRepository.requestSingleUpdate()

            // Get current location (might be cached or newly updated)
            val location = locationRepository.getLastKnownLocation()

            // Create message
            val seq = seqCounter++
            pendingAckSeq = seq

            val message = if (location != null) {
                Message.TextMessage(
                    seq = seq,
                    text = text.uppercase(),
                    hasGps = true,
                    latitude = location.latitude,
                    longitude = location.longitude
                )
            } else {
                Message.TextMessage(
                    seq = seq,
                    text = text.uppercase(),
                    hasGps = false
                )
            }

            // Add to chat UI
            val chatMessage = ChatMessage(
                text = text.uppercase(),
                isSent = true,
                seq = seq,
                ackStatus = AckStatus.PENDING,
                hasGps = location != null,
                latitude = location?.latitude,
                longitude = location?.longitude
            )
            messageRepository.addMessage(chatMessage)

            // Send via BLE
            val success = bleRepository.sendMessage(message)

            if (!success) {
                Log.e(TAG, "Failed to send message")
                _toastMessage.emit("Send failed - will retry")

                // Retry after delay
                delay(1000)
                if (bleRepository.isConnected()) {
                    bleRepository.sendMessage(message)
                }
            } else {
                Log.d(TAG, "Message sent successfully")

                // Disable send button until ACK or timeout
                _uiState.value = _uiState.value.copy(canSendMessage = false)

                // Schedule ACK timeout
                scheduleAckTimeout(seq)
            }
        }
    }

    /**
     * Handle received message
     * @see UC-3.2: Handle ACK Reception
     * @see UC-4.1: Receive Text Message
     */
    private fun handleReceivedMessage(message: Message) {
        when (message) {
            is Message.TextMessage -> {
                Log.d(TAG, "Text message received: ${message.text}")

                val chatMessage = ChatMessage(
                    text = message.text,
                    isSent = false,
                    seq = message.seq,
                    ackStatus = AckStatus.NONE,
                    hasGps = message.hasGps,
                    latitude = message.latitude,
                    longitude = message.longitude
                )
                messageRepository.addMessage(chatMessage)
            }

            is Message.AckMessage -> {
                Log.d(TAG, "ACK received for seq: ${message.seq}")

                // Update message status
                messageRepository.updateAckStatus(message.seq, AckStatus.DELIVERED)

                // Re-enable send button if this is the pending ACK
                if (pendingAckSeq == message.seq) {
                    pendingAckSeq = null
                    ackTimeoutJob?.cancel()
                    _uiState.value = _uiState.value.copy(canSendMessage = true)
                    _toastMessage.tryEmit("✓ Message delivered (seq ${message.seq})")
                }
            }
        }
    }

    /**
     * Schedule ACK timeout (5 seconds)
     * @see UC-3.3: Handle ACK Timeout
     */
    private fun scheduleAckTimeout(seq: Byte) {
        ackTimeoutJob?.cancel()
        ackTimeoutJob = viewModelScope.launch {
            delay(BleConstants.ACK_TIMEOUT_MS)
            if (pendingAckSeq == seq) {
                Log.d(TAG, "ACK timeout for seq $seq - marking as failed")
                pendingAckSeq = null
                messageRepository.updateAckStatus(seq, AckStatus.FAILED)
                _uiState.value = _uiState.value.copy(canSendMessage = true)
                _toastMessage.tryEmit("Message delivery failed (no ACK)")
            }
        }
    }

    /**
     * Update message input text and character count
     * @see UC-3.4: Validate Message Input
     */
    fun updateMessageInput(text: String) {
        val charCount = text.length
        val packedBytes = LoRaProtocol.calculatePackedSize(text)
        val totalBytes = 12 + packedBytes  // Header + packed text (without GPS)

        _uiState.value = _uiState.value.copy(
            messageInput = text,
            charCount = charCount,
            charCountText = "$charCount/${Message.MAX_TEXT_LENGTH} chars ($totalBytes bytes)"
        )
    }

    override fun onCleared() {
        super.onCleared()
        bleRepository.onDestroy()
        locationRepository.onDestroy()
    }
}

/**
 * UI State for chat screen
 */
data class ChatUiState(
    val connectionState: BleConnectionState = BleConnectionState.Disconnected,
    val connectionStatusText: String = "❌ Disconnected",
    val messages: List<ChatMessage> = emptyList(),
    val gpsText: String = "No GPS fix",
    val messageInput: String = "",
    val charCount: Int = 0,
    val charCountText: String = "0/50 chars (12 bytes)",
    val canSendMessage: Boolean = false,
    val discoveredDevices: List<BleRepository.DiscoveredDevice> = emptyList(),
    val batteryLevel: Int? = null
)
