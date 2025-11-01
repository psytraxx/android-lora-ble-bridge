package com.example.lorabridge.data.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.ParcelUuid
import android.util.Log
import com.example.lorabridge.data.protocol.LoRaProtocol
import com.example.lorabridge.domain.model.BleConnectionState
import com.example.lorabridge.domain.model.Message
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Repository for BLE operations using coroutines and StateFlow
 * Manages connection lifecycle, scanning, and message transmission
 */
@Singleton
class BleRepository @Inject constructor(
    @param:ApplicationContext private val context: Context
) {
    private val scope = CoroutineScope(Dispatchers.IO + Job())

    private val bluetoothManager =
        context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager.adapter
    private val bluetoothLeScanner: BluetoothLeScanner? = bluetoothAdapter?.bluetoothLeScanner

    // State
    private val _connectionState =
        MutableStateFlow<BleConnectionState>(BleConnectionState.Disconnected)
    val connectionState: StateFlow<BleConnectionState> = _connectionState.asStateFlow()

    private val _receivedMessages = MutableSharedFlow<Message>(extraBufferCapacity = 10)
    val receivedMessages = _receivedMessages.asSharedFlow()

    // Discovered devices
    data class DiscoveredDevice(
        val device: BluetoothDevice,
        val name: String?,
        val address: String,
        val rssi: Int
    )

    private val _discoveredDevices = MutableStateFlow<List<DiscoveredDevice>>(emptyList())
    val discoveredDevices: StateFlow<List<DiscoveredDevice>> = _discoveredDevices.asStateFlow()

    // BLE GATT objects
    private var bluetoothGatt: BluetoothGatt? = null
    private var txCharacteristic: BluetoothGattCharacteristic? = null  // Receive notifications
    private var rxCharacteristic: BluetoothGattCharacteristic? = null  // Send messages

    // Scanning
    private var currentScanCallback: ScanCallback? = null
    private var scanJob: Job? = null

    // Auto-disconnect
    private var autoDisconnectJob: Job? = null

    companion object {
        private const val TAG = "BleRepository"
    }

    /**
     * Check if connected to BLE device
     */
    fun isConnected(): Boolean = _connectionState.value is BleConnectionState.Connected

    /**
     * Start BLE scan for ESP32S3-LoRa device
     * @see UC-1.1: Scan for ESP32S3 Device
     */
    @SuppressLint("MissingPermission")
    fun startScan() {
        if (!isBluetoothEnabled()) {
            _connectionState.value = BleConnectionState.Error("Bluetooth not enabled")
            return
        }

        if (bluetoothLeScanner == null) {
            _connectionState.value = BleConnectionState.Error("BLE scanner not available")
            return
        }

        stopScan()

        // Clear discovered devices list
        _discoveredDevices.value = emptyList()

        _connectionState.value = BleConnectionState.Scanning
        Log.d(TAG, "Starting BLE scan for devices with service UUID: ${BleConstants.SERVICE_UUID}")

        // Scan filter - only devices advertising our service UUID
        val scanFilter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(BleConstants.SERVICE_UUID))
            .build()

        val scanSettings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .setCallbackType(ScanSettings.CALLBACK_TYPE_ALL_MATCHES)
            .setMatchMode(ScanSettings.MATCH_MODE_AGGRESSIVE)
            .setNumOfMatches(ScanSettings.MATCH_NUM_ONE_ADVERTISEMENT)
            .setReportDelay(0)
            .build()

        currentScanCallback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val deviceName = result.device.name
                val deviceAddress = result.device.address
                Log.d(
                    TAG,
                    "BLE device found with service UUID: name='$deviceName', address=$deviceAddress, rssi=${result.rssi}"
                )

                // Add to discovered devices list if not already present
                val currentList = _discoveredDevices.value
                if (currentList.none { it.address == deviceAddress }) {
                    val discoveredDevice = DiscoveredDevice(
                        device = result.device,
                        name = deviceName,
                        address = deviceAddress,
                        rssi = result.rssi
                    )
                    _discoveredDevices.value = currentList + discoveredDevice
                    Log.d(
                        TAG,
                        "Added device to list. Total devices: ${_discoveredDevices.value.size}"
                    )
                }
            }

            override fun onScanFailed(errorCode: Int) {
                Log.e(TAG, "Scan failed: $errorCode")
                _connectionState.value = BleConnectionState.Error("Scan failed (code: $errorCode)")
            }
        }

        bluetoothLeScanner.startScan(listOf(scanFilter), scanSettings, currentScanCallback)

        // Scan timeout - stop scanning after 30 seconds if no connection made
        scanJob = scope.launch {
            delay(BleConstants.SCAN_TIMEOUT_MS)
            if (_connectionState.value is BleConnectionState.Scanning) {
                stopScan()
                if (_discoveredDevices.value.isEmpty()) {
                    _connectionState.value =
                        BleConnectionState.Error("No devices found", canRetry = true)
                }
                // If devices were found but user didn't connect, keep them visible in the dialog
            }
        }
    }

    /**
     * Stop BLE scan
     */
    @SuppressLint("MissingPermission")
    fun stopScan() {
        scanJob?.cancel()
        currentScanCallback?.let {
            try {
                bluetoothLeScanner?.stopScan(it)
                Log.d(TAG, "Scan stopped")
            } catch (e: Exception) {
                Log.e(TAG, "Error stopping scan", e)
            }
            currentScanCallback = null
        }
    }

    /**
     * Connect to a specific device by address
     */
    fun connectToDevice(deviceAddress: String) {
        val device = _discoveredDevices.value.find { it.address == deviceAddress }?.device
        if (device != null) {
            stopScan()
            connectToDevice(device)
        } else {
            _connectionState.value = BleConnectionState.Error("Device not found")
        }
    }

    /**
     * Connect to BLE device
     * @see UC-1.2: Connect to ESP32S3 Device
     */
    @SuppressLint("MissingPermission")
    private fun connectToDevice(device: BluetoothDevice) {
        _connectionState.value = BleConnectionState.Connecting
        Log.d(TAG, "Connecting to ${device.address}")

        bluetoothGatt = device.connectGatt(context, false, gattCallback)

        // Connection timeout
        scope.launch {
            withTimeoutOrNull(BleConstants.CONNECTION_TIMEOUT_MS) {
                // Wait for connection
                while (_connectionState.value !is BleConnectionState.Connected &&
                    _connectionState.value !is BleConnectionState.Error
                ) {
                    delay(100)
                }
            } ?: run {
                if (_connectionState.value !is BleConnectionState.Connected) {
                    disconnect()
                    _connectionState.value = BleConnectionState.Error("Connection timeout")
                }
            }
        }
    }

    /**
     * GATT callback for connection events
     */
    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when {
                newState == BluetoothGatt.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS -> {
                    Log.d(TAG, "GATT connected, requesting MTU")
                    _connectionState.value = BleConnectionState.NegotiatingMtu
                    gatt.requestMtu(BleConstants.MTU_SIZE)
                }

                newState == BluetoothGatt.STATE_DISCONNECTED -> {
                    Log.d(TAG, "GATT disconnected")
                    cleanup()

                    // Clear discovered devices so list is fresh on reconnect
                    _discoveredDevices.value = emptyList()

                    _connectionState.value = BleConnectionState.Disconnected
                }

                status != BluetoothGatt.GATT_SUCCESS -> {
                    Log.e(TAG, "Connection failed: status=$status")
                    cleanup()
                    _connectionState.value = BleConnectionState.Error("Connection failed")
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "MTU negotiated: $mtu bytes")
            } else {
                Log.w(TAG, "MTU negotiation failed, continuing with default")
            }
            _connectionState.value = BleConnectionState.DiscoveringServices
            gatt.discoverServices()
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(TAG, "Service discovery failed")
                disconnect()
                _connectionState.value = BleConnectionState.Error("Service discovery failed")
                return
            }

            val service = gatt.getService(BleConstants.SERVICE_UUID)
            if (service == null) {
                Log.e(TAG, "LoRa service not found")
                disconnect()
                _connectionState.value = BleConnectionState.Error("LoRa service not found")
                return
            }

            txCharacteristic = service.getCharacteristic(BleConstants.TX_CHAR_UUID)
            rxCharacteristic = service.getCharacteristic(BleConstants.RX_CHAR_UUID)

            if (txCharacteristic == null || rxCharacteristic == null) {
                Log.e(TAG, "Required characteristics not found")
                disconnect()
                _connectionState.value = BleConnectionState.Error("Characteristics not found")
                return
            }

            // Enable notifications
            _connectionState.value = BleConnectionState.EnablingNotifications
            gatt.setCharacteristicNotification(txCharacteristic, true)

            val descriptor = txCharacteristic?.getDescriptor(BleConstants.CCCD_UUID)
            if (descriptor != null) {
                descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(descriptor)
            } else {
                Log.e(TAG, "CCCD descriptor not found")
                disconnect()
                _connectionState.value = BleConnectionState.Error("CCCD descriptor not found")
            }
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "Notifications enabled - connection complete!")
                _connectionState.value = BleConnectionState.Connected
            } else {
                Log.e(TAG, "Failed to enable notifications")
                disconnect()
                _connectionState.value = BleConnectionState.Error("Failed to enable notifications")
            }
        }

        @Deprecated("Deprecated in API 33")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            // Only handle in the deprecated callback for API < 33
            if (android.os.Build.VERSION.SDK_INT < 33 && characteristic.uuid == BleConstants.TX_CHAR_UUID) {
                handleReceivedData(characteristic.value)
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            // Only handle in the new callback for API >= 33
            if (android.os.Build.VERSION.SDK_INT >= 33 && characteristic.uuid == BleConstants.TX_CHAR_UUID) {
                handleReceivedData(value)
            }
        }
    }

    /**
     * Handle received BLE data
     */
    private fun handleReceivedData(data: ByteArray) {
        Log.d(TAG, "Received ${data.size} bytes")
        try {
            val message = LoRaProtocol.deserialize(data)
            Log.d(TAG, "Deserialized: $message")
            scope.launch {
                _receivedMessages.emit(message)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to deserialize message", e)
        }
    }

    /**
     * Send message via BLE
     */
    @SuppressLint("MissingPermission")
    fun sendMessage(message: Message): Boolean {
        if (!isConnected()) {
            Log.e(TAG, "Cannot send: not connected")
            return false
        }

        val gatt = bluetoothGatt ?: return false
        val rxChar = rxCharacteristic ?: return false

        val data = LoRaProtocol.serialize(message)
        Log.d(TAG, "Sending ${data.size} bytes")

        rxChar.value = data
        val success = gatt.writeCharacteristic(rxChar)

        if (success) {
            scheduleAutoDisconnect()
        }

        return success
    }

    /**
     * Schedule auto-disconnect after inactivity
     * @see UC-1.4: Auto-Disconnect After Inactivity
     */
    private fun scheduleAutoDisconnect() {
        autoDisconnectJob?.cancel()
        autoDisconnectJob = scope.launch {
            delay(BleConstants.AUTO_DISCONNECT_DELAY_MS)
            if (isConnected()) {
                Log.d(TAG, "Auto-disconnecting after inactivity")
                disconnect()
            }
        }
    }

    /**
     * Disconnect BLE
     */
    @SuppressLint("MissingPermission")
    fun disconnect() {
        Log.d(TAG, "Disconnecting BLE")
        stopScan()
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        cleanup()

        // Clear discovered devices on disconnect
        _discoveredDevices.value = emptyList()

        _connectionState.value = BleConnectionState.Disconnected
    }

    /**
     * Clean up resources
     */
    private fun cleanup() {
        autoDisconnectJob?.cancel()
        bluetoothGatt = null
        txCharacteristic = null
        rxCharacteristic = null
    }

    /**
     * Check if Bluetooth is enabled
     */
    private fun isBluetoothEnabled(): Boolean = bluetoothAdapter?.isEnabled == true

    /**
     * Clean up when repository is destroyed
     */
    fun onDestroy() {
        disconnect()
        scope.cancel()
    }
}
