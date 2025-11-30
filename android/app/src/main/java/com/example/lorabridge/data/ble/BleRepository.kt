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
        val rssi: Int,
        val lastSeenTimestamp: Long = System.currentTimeMillis()
    )

    private val _discoveredDevices = MutableStateFlow<List<DiscoveredDevice>>(emptyList())
    val discoveredDevices: StateFlow<List<DiscoveredDevice>> = _discoveredDevices.asStateFlow()

    // BLE GATT objects
    private var bluetoothGatt: BluetoothGatt? = null
    private var txCharacteristic: BluetoothGattCharacteristic? = null  // Receive notifications
    private var rxCharacteristic: BluetoothGattCharacteristic? = null  // Send messages
    private var batteryCharacteristic: BluetoothGattCharacteristic? = null  // Battery level

    // Battery level state
    private val _batteryLevel = MutableStateFlow<Int?>(null)
    val batteryLevel: StateFlow<Int?> = _batteryLevel.asStateFlow()

    // Scanning
    private var currentScanCallback: ScanCallback? = null
    private var deviceCleanupJob: Job? = null
    private var scanTimeoutJob: Job? = null
    private var shouldBeScanning: Boolean = false  // Track if scanning should be active

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
        shouldBeScanning = true

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
            //.setMatchMode(ScanSettings.MATCH_MODE_AGGRESSIVE)
            .setNumOfMatches(ScanSettings.MATCH_NUM_ONE_ADVERTISEMENT)
            .setReportDelay(0)
            .build()

        currentScanCallback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                // Ignore callbacks if we're not actively scanning
                if (_connectionState.value !is BleConnectionState.Scanning) {
                    return
                }

                val deviceName = result.device.name
                val deviceAddress = result.device.address
                Log.d(
                    TAG,
                    "BLE device found with service UUID: name='$deviceName', address=$deviceAddress, rssi=${result.rssi}"
                )

                // Update or add device to discovered devices list
                val currentList = _discoveredDevices.value
                val existingDeviceIndex = currentList.indexOfFirst { it.address == deviceAddress }

                if (existingDeviceIndex >= 0) {
                    // Update existing device with new RSSI and timestamp
                    val updatedDevice = currentList[existingDeviceIndex].copy(
                        rssi = result.rssi,
                        lastSeenTimestamp = System.currentTimeMillis()
                    )
                    _discoveredDevices.value = currentList.toMutableList().apply {
                        set(existingDeviceIndex, updatedDevice)
                    }
                    Log.d(TAG, "Updated device timestamp. Total devices: ${_discoveredDevices.value.size}")
                } else {
                    // Add new device
                    val discoveredDevice = DiscoveredDevice(
                        device = result.device,
                        name = deviceName,
                        address = deviceAddress,
                        rssi = result.rssi,
                        lastSeenTimestamp = System.currentTimeMillis()
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

        // Start periodic cleanup of stale devices
        startDeviceCleanup()

        // Scan timeout - stop scanning after 30 seconds if no connection made
        scanTimeoutJob = scope.launch {
            delay(BleConstants.SCAN_TIMEOUT_MS)
            if (_connectionState.value is BleConnectionState.Scanning) {
                stopScan()
                val devicesFound = _discoveredDevices.value.isNotEmpty()

                // Clear device list on timeout
                _discoveredDevices.value = emptyList()

                // Show appropriate error message
                _connectionState.value = if (devicesFound) {
                    BleConnectionState.Error("Scan timeout - please retry", canRetry = true)
                } else {
                    BleConnectionState.Error("No devices found", canRetry = true)
                }
            }
        }
    }

    /**
     * Start periodic cleanup of stale devices (devices not seen in 15 seconds)
     * Only cleans up while actively scanning
     */
    private fun startDeviceCleanup() {
        deviceCleanupJob?.cancel()
        deviceCleanupJob = scope.launch {
            while (true) {
                delay(3_000L)  // Check every 3 seconds

                // Only clean up if we're actively scanning
                if (currentScanCallback != null && _connectionState.value is BleConnectionState.Scanning) {
                    val currentTime = System.currentTimeMillis()
                    val currentList = _discoveredDevices.value

                    val filteredList = currentList.filter {
                        currentTime - it.lastSeenTimestamp < BleConstants.DEVICE_STALE_TIMEOUT_MS
                    }

                    if (filteredList.size != currentList.size) {
                        val removedCount = currentList.size - filteredList.size
                        Log.d(TAG, "Removed $removedCount stale device(s). Remaining: ${filteredList.size}")
                        _discoveredDevices.value = filteredList
                    }
                }
            }
        }
    }

    /**
     * Stop BLE scan
     */
    @SuppressLint("MissingPermission")
    fun stopScan() {
        shouldBeScanning = false
        scanTimeoutJob?.cancel()
        deviceCleanupJob?.cancel()
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
     * Pause scanning (app going to background)
     */
    @SuppressLint("MissingPermission")
    fun pauseScan() {
        if (currentScanCallback != null) {
            scanTimeoutJob?.cancel()
            deviceCleanupJob?.cancel()
            try {
                bluetoothLeScanner?.stopScan(currentScanCallback)
                Log.d(TAG, "Scan paused (app backgrounded)")
            } catch (e: Exception) {
                Log.e(TAG, "Error pausing scan", e)
            }
            currentScanCallback = null
        }
    }

    /**
     * Resume scanning (app coming to foreground)
     */
    fun resumeScan() {
        if (shouldBeScanning && currentScanCallback == null && !isConnected()) {
            Log.d(TAG, "Resuming scan (app foregrounded)")
            startScan()
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
        // Clean up any existing connection first
        bluetoothGatt?.let {
            Log.d(TAG, "Cleaning up existing GATT connection before connecting")
            it.disconnect()
            it.close()
            cleanup()
        }

        _connectionState.value = BleConnectionState.Connecting
        Log.d(TAG, "Connecting to ${device.name ?: "Unknown"} (${device.address})")

        bluetoothGatt = device.connectGatt(context, false, gattCallback)

        // Connection timeout with phase tracking
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
                    // Provide specific error based on what phase we were in
                    val currentPhase = _connectionState.value
                    val errorMsg = when (currentPhase) {
                        is BleConnectionState.Connecting -> "Connection timeout - device not responding"
                        is BleConnectionState.NegotiatingMtu -> "Connection timeout - MTU negotiation failed"
                        is BleConnectionState.DiscoveringServices -> "Connection timeout - service discovery timeout"
                        is BleConnectionState.EnablingNotifications -> "Connection timeout - notification setup failed"
                        else -> "Connection timeout after 30 seconds"
                    }

                    Log.e(TAG, "$errorMsg (was in state: $currentPhase)")
                    disconnect()
                    _connectionState.value = BleConnectionState.Error(errorMsg, canRetry = true)
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
                Log.i(TAG, "MTU negotiated successfully: $mtu bytes")
            } else {
                Log.w(TAG, "MTU negotiation failed (status=$status), continuing with default MTU")
            }
            _connectionState.value = BleConnectionState.DiscoveringServices
            Log.d(TAG, "Starting service discovery...")

            // Add small delay before service discovery to improve reliability on some devices
            scope.launch {
                delay(50)
                gatt.discoverServices()
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(TAG, "Service discovery failed (status=$status)")
                disconnect()
                _connectionState.value = BleConnectionState.Error("Service discovery failed (status=$status)", canRetry = true)
                return
            }

            Log.i(TAG, "Services discovered successfully, found ${gatt.services.size} services")

            val service = gatt.getService(BleConstants.SERVICE_UUID)
            if (service == null) {
                Log.e(TAG, "LoRa service (${BleConstants.SERVICE_UUID}) not found")
                gatt.services.forEach { s ->
                    Log.d(TAG, "  Available service: ${s.uuid}")
                }
                disconnect()
                _connectionState.value = BleConnectionState.Error("LoRa service not found", canRetry = true)
                return
            }

            Log.d(TAG, "LoRa service found")

            txCharacteristic = service.getCharacteristic(BleConstants.TX_CHAR_UUID)
            rxCharacteristic = service.getCharacteristic(BleConstants.RX_CHAR_UUID)

            if (txCharacteristic == null || rxCharacteristic == null) {
                Log.e(TAG, "Required characteristics not found")
                disconnect()
                _connectionState.value = BleConnectionState.Error("Characteristics not found")
                return
            }

            // Get battery service (optional - don't fail if not present)
            val batteryService = gatt.getService(BleConstants.BATTERY_SERVICE_UUID)
            if (batteryService != null) {
                batteryCharacteristic = batteryService.getCharacteristic(BleConstants.BATTERY_LEVEL_UUID)
                if (batteryCharacteristic != null) {
                    Log.d(TAG, "Battery service found - reading battery level")
                    // Read battery level first - must serialize BLE operations!
                    // onCharacteristicRead will trigger TX notification setup
                    gatt.readCharacteristic(batteryCharacteristic)
                    return
                }
            } else {
                Log.d(TAG, "Battery service not available on this device")
            }

            // If no battery service, start TX notifications immediately
            enableTxNotifications(gatt)
        }

        @SuppressLint("MissingPermission")
        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(TAG, "Failed to write descriptor for ${descriptor.characteristic.uuid}")
                disconnect()
                _connectionState.value = BleConnectionState.Error("Failed to enable notifications")
                return
            }

            when (descriptor.characteristic.uuid) {
                BleConstants.TX_CHAR_UUID -> {
                    Log.d(TAG, "TX notifications enabled")

                    // Now enable battery notifications if available
                    val batteryChar = batteryCharacteristic
                    if (batteryChar != null) {
                        gatt.setCharacteristicNotification(batteryChar, true)
                        val batteryDescriptor = batteryChar.getDescriptor(BleConstants.CCCD_UUID)
                        if (batteryDescriptor != null) {
                            // Use new API for Android 13+ (API 33+)
                            if (android.os.Build.VERSION.SDK_INT >= 33) {
                                gatt.writeDescriptor(batteryDescriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                            } else {
                                @Suppress("DEPRECATION")
                                batteryDescriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                                @Suppress("DEPRECATION")
                                gatt.writeDescriptor(batteryDescriptor)
                            }
                            Log.d(TAG, "Enabling battery notifications...")
                        } else {
                            Log.w(TAG, "Battery CCCD descriptor not found, connection complete without battery notifications")
                            _connectionState.value = BleConnectionState.Connected
                        }
                    } else {
                        // No battery characteristic, we're done
                        Log.d(TAG, "Notifications enabled - connection complete!")
                        _connectionState.value = BleConnectionState.Connected
                    }
                }

                BleConstants.BATTERY_LEVEL_UUID -> {
                    Log.d(TAG, "Battery notifications enabled - connection complete!")
                    _connectionState.value = BleConnectionState.Connected
                }

                else -> {
                    Log.d(TAG, "Descriptor write complete for ${descriptor.characteristic.uuid}")
                }
            }
        }

        @Deprecated("Deprecated in API 33")
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            // Only handle in the deprecated callback for API < 33
            if (android.os.Build.VERSION.SDK_INT < 33) {
                when (characteristic.uuid) {
                    BleConstants.TX_CHAR_UUID -> handleReceivedData(characteristic.value)
                    BleConstants.BATTERY_LEVEL_UUID -> handleBatteryLevelUpdate(characteristic.value)
                }
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            // Only handle in the new callback for API >= 33
            if (android.os.Build.VERSION.SDK_INT >= 33) {
                when (characteristic.uuid) {
                    BleConstants.TX_CHAR_UUID -> handleReceivedData(value)
                    BleConstants.BATTERY_LEVEL_UUID -> handleBatteryLevelUpdate(value)
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS && characteristic.uuid == BleConstants.BATTERY_LEVEL_UUID) {
                handleBatteryLevelUpdate(value)
                // Battery read complete - now enable TX notifications
                enableTxNotifications(gatt)
            }
        }

        /**
         * Enable TX characteristic notifications
         * Must be called after all read operations are complete to avoid BLE operation queue conflicts
         */
        @SuppressLint("MissingPermission")
        private fun enableTxNotifications(gatt: BluetoothGatt) {
            _connectionState.value = BleConnectionState.EnablingNotifications
            gatt.setCharacteristicNotification(txCharacteristic, true)

            val descriptor = txCharacteristic?.getDescriptor(BleConstants.CCCD_UUID)
            if (descriptor != null) {
                // Use new API for Android 13+ (API 33+)
                val writeResult = if (android.os.Build.VERSION.SDK_INT >= 33) {
                    gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                } else {
                    @Suppress("DEPRECATION")
                    descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    @Suppress("DEPRECATION")
                    gatt.writeDescriptor(descriptor)
                }

                Log.d(TAG, "TX writeDescriptor initiated, result: $writeResult")

                if (writeResult == BluetoothGatt.GATT_WRITE_NOT_PERMITTED || writeResult == false) {
                    Log.e(TAG, "Failed to initiate TX CCCD descriptor write")
                    disconnect()
                    _connectionState.value = BleConnectionState.Error("Failed to write TX CCCD descriptor")
                }
            } else {
                Log.e(TAG, "TX CCCD descriptor not found")
                disconnect()
                _connectionState.value = BleConnectionState.Error("TX CCCD descriptor not found")
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
     * Handle battery level update from BLE characteristic
     */
    private fun handleBatteryLevelUpdate(data: ByteArray) {
        if (data.isNotEmpty()) {
            val batteryLevel = data[0].toInt() and 0xFF  // Convert to unsigned int (0-100)
            Log.d(TAG, "Battery level: $batteryLevel%")
            _batteryLevel.value = batteryLevel
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

        // Use new API for Android 13+ (API 33+)
        val success = if (android.os.Build.VERSION.SDK_INT >= 33) {
            gatt.writeCharacteristic(rxChar, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothGatt.GATT_SUCCESS
        } else {
            @Suppress("DEPRECATION")
            rxChar.value = data
            @Suppress("DEPRECATION")
            gatt.writeCharacteristic(rxChar)
        }

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
        batteryCharacteristic = null
        _batteryLevel.value = null
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
