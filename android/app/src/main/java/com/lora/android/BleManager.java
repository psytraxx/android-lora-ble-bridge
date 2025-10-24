package com.lora.android;

import android.Manifest;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.content.Context;
import android.content.pm.PackageManager;
import android.location.LocationManager;
import android.util.Log;

import androidx.core.app.ActivityCompat;
import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;

import java.util.UUID;

import lora.Protocol;

public class BleManager {

    private static final String TAG = "BleManager";
    private static final String DEVICE_NAME = "ESP32S3-LoRa";
    private static final UUID SERVICE_UUID = UUID.fromString("00001234-0000-1000-8000-00805F9B34FB");
    private static final UUID TX_CHAR_UUID = UUID.fromString("00005678-0000-1000-8000-00805F9B34FB");
    private static final UUID RX_CHAR_UUID = UUID.fromString("00005679-0000-1000-8000-00805F9B34FB");
    private static final UUID CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private static final long SCAN_TIMEOUT_MS = 15000; // 15 seconds scan timeout
    private static BleManager instance;
    private final Context context;
    private final android.os.Handler mainHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    private final android.os.Handler locationCheckHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    // LiveData for state changes
    private final MutableLiveData<String> connectionStatus = new MutableLiveData<>();
    private final MutableLiveData<Protocol.Message> messageReceived = new MutableLiveData<>();
    private final MutableLiveData<String> showToast = new MutableLiveData<>();
    private final MutableLiveData<Boolean> connected = new MutableLiveData<>();
    private BluetoothAdapter bluetoothAdapter;
    private BluetoothLeScanner bluetoothLeScanner;
    private BluetoothGatt bluetoothGatt;
    private BluetoothGattCharacteristic txCharacteristic;
    private BluetoothGattCharacteristic rxCharacteristic;
    private boolean isWaitingForLocation = false;
    private boolean isScanning = false;
    private ScanCallback currentScanCallback = null;
    private android.content.BroadcastReceiver locationProviderReceiver;

    private BleManager(Context context) {
        this.context = context;
        initializeBluetooth();
    }

    public static synchronized BleManager getInstance(Context context) {
        if (instance == null) {
            instance = new BleManager(context.getApplicationContext());
        }
        return instance;
    }

    private void cleanupHandlers() {
        mainHandler.removeCallbacksAndMessages(null);
        locationCheckHandler.removeCallbacksAndMessages(null);
        unregisterLocationProviderReceiver();
    }

    // LiveData getters
    public LiveData<String> getConnectionStatus() {
        return connectionStatus;
    }

    public LiveData<Protocol.Message> getMessageReceived() {
        return messageReceived;
    }

    public LiveData<String> getShowToast() {
        return showToast;
    }

    public LiveData<Boolean> getConnected() {
        return connected;
    }

    private void initializeBluetooth() {
        BluetoothManager bluetoothManager = (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
        bluetoothAdapter = bluetoothManager.getAdapter();
        if (bluetoothAdapter != null) {
            bluetoothLeScanner = bluetoothAdapter.getBluetoothLeScanner();
        }
    }

    public boolean isBluetoothEnabled() {
        return bluetoothAdapter != null && bluetoothAdapter.isEnabled();
    }

    public boolean hasPermissions() {
        return ActivityCompat.checkSelfPermission(context,
                Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED &&
                ActivityCompat.checkSelfPermission(context,
                        Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
    }

    public boolean isLocationEnabled() {
        LocationManager locationManager = (LocationManager) context.getSystemService(Context.LOCATION_SERVICE);
        if (locationManager == null) {
            return false;
        }
        return locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER) ||
                locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER);
    }

    @SuppressLint("MissingPermission")
    public void startScan() {
        if (!hasPermissions()) {
            connectionStatus.postValue("❌ BT permissions missing");
            return;
        }

        if (!isBluetoothEnabled()) {
            connectionStatus.postValue("❌ BT not enabled");
            return;
        }

        if (!isLocationEnabled()) {
            connectionStatus.postValue("❌ Location services disabled (required for BLE)");
            showToast.postValue("Please enable Location services to scan for BLE devices");
            Log.w(TAG,
                    "Location services are disabled. BLE scanning requires location services to be enabled on Android.");

            // Register receiver to listen for location changes
            if (!isWaitingForLocation) {
                isWaitingForLocation = true;
                registerLocationProviderReceiver();
            }
            return;
        }

        // Location is enabled, stop waiting
        isWaitingForLocation = false;
        // Stop any existing scan
        stopScan();

        Log.d(TAG, "Starting BLE scan for device: " + DEVICE_NAME);
        connectionStatus.postValue("🔍 Scanning...");
        isScanning = true;

        currentScanCallback = new ScanCallback() {
            @Override
            public void onScanResult(int callbackType, ScanResult result) {
                BluetoothDevice device = result.getDevice();
                String deviceName = device.getName();
                Log.d(TAG, "Found BLE device: " + deviceName + " (" + device.getAddress() + ")");

                if (DEVICE_NAME.equals(deviceName)) {
                    Log.d(TAG, "Target device found! Connecting...");
                    connectionStatus.postValue("📡 Connecting...");
                    stopScan();
                    connectToDevice(device);
                }
            }

            @Override
            public void onScanFailed(int errorCode) {
                Log.e(TAG, "BLE scan failed with error code: " + errorCode);
                connectionStatus.postValue("❌ Scan failed (error " + errorCode + ")");
                isScanning = false;
                currentScanCallback = null;
            }
        };

        // Use scan filters and settings for faster, more efficient scanning
        android.bluetooth.le.ScanSettings scanSettings = new android.bluetooth.le.ScanSettings.Builder()
                .setScanMode(android.bluetooth.le.ScanSettings.SCAN_MODE_LOW_LATENCY) // Fastest scanning
                .setCallbackType(android.bluetooth.le.ScanSettings.CALLBACK_TYPE_ALL_MATCHES)
                .setMatchMode(android.bluetooth.le.ScanSettings.MATCH_MODE_AGGRESSIVE) // Match ASAP
                .setNumOfMatches(android.bluetooth.le.ScanSettings.MATCH_NUM_ONE_ADVERTISEMENT) // Stop after first match
                .setReportDelay(0) // Report immediately
                .build();

        // Filter by device name to ignore other BLE devices
        android.bluetooth.le.ScanFilter nameFilter = new android.bluetooth.le.ScanFilter.Builder()
                .setDeviceName(DEVICE_NAME)
                .build();

        java.util.List<android.bluetooth.le.ScanFilter> filters = new java.util.ArrayList<>();
        filters.add(nameFilter);

        Log.d(TAG, "Starting optimized scan with filters (device name: " + DEVICE_NAME + ")");
        bluetoothLeScanner.startScan(filters, scanSettings, currentScanCallback);

        // Set scan timeout
        mainHandler.postDelayed(() -> {
            if (isScanning) {
                Log.w(TAG, "Scan timeout - device not found");
                stopScan();
                connectionStatus.postValue("❌ Device not found");
            }
        }, SCAN_TIMEOUT_MS);
    }

    @SuppressLint("MissingPermission")
    private void stopScan() {
        if (isScanning && currentScanCallback != null) {
            try {
                bluetoothLeScanner.stopScan(currentScanCallback);
                Log.d(TAG, "Scan stopped");
            } catch (Exception e) {
                Log.e(TAG, "Error stopping scan: " + e.getMessage());
            }
            isScanning = false;
            currentScanCallback = null;
        }
    }

    public void registerLocationProviderReceiver() {
        if (locationProviderReceiver != null)
            return;
        locationProviderReceiver = new android.content.BroadcastReceiver() {
            @Override
            public void onReceive(Context context, android.content.Intent intent) {
                if (LocationManager.PROVIDERS_CHANGED_ACTION.equals(intent.getAction())) {
                    if (isLocationEnabled() && isWaitingForLocation && !isConnected()) {
                        Log.d(TAG, "Location services now enabled! Retrying BLE scan...");
                        showToast.postValue("Location enabled! Scanning for device...");
                        startScan();
                    }
                }
            }
        };
        android.content.IntentFilter filter = new android.content.IntentFilter(
                LocationManager.PROVIDERS_CHANGED_ACTION);
        context.registerReceiver(locationProviderReceiver, filter);
    }

    public void unregisterLocationProviderReceiver() {
        if (locationProviderReceiver != null) {
            try {
                context.unregisterReceiver(locationProviderReceiver);
            } catch (Exception e) {
                Log.e(TAG, "Error unregistering locationProviderReceiver: " + e.getMessage());
            }
            locationProviderReceiver = null;
        }
    }

    public void onDestroy() {
        cleanupHandlers();
    }

    @SuppressLint("MissingPermission")
    private void connectToDevice(BluetoothDevice device) {
        Log.d(TAG, "Connecting to device: " + device.getAddress());
        bluetoothGatt = device.connectGatt(context, false, new BluetoothGattCallback() {
            @Override
            public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
                Log.d(TAG, "Connection state changed: status=" + status + ", newState=" + newState);

                if (newState == BluetoothGatt.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                    // Don't set connected=true yet - wait for service discovery to complete
                    Log.d(TAG, "Connected! Requesting MTU...");
                    connectionStatus.postValue("🔗 Negotiating...");
                    gatt.requestMtu(512);
                } else if (newState == BluetoothGatt.STATE_DISCONNECTED) {
                    // Handle disconnection (device powered off, out of range, etc.)
                    Log.d(TAG, "Disconnected. Status: " + status + ". Cleaning up GATT...");
                    connected.postValue(false);
                    connectionStatus.postValue("❌ Disconnected");

                    // Clean up GATT connection and characteristics
                    txCharacteristic = null;
                    rxCharacteristic = null;

                    // Close the GATT connection to release resources
                    if (bluetoothGatt != null) {
                        bluetoothGatt.close();
                        bluetoothGatt = null;
                    }
                } else if (status != BluetoothGatt.GATT_SUCCESS) {
                    // Connection failed
                    Log.e(TAG, "Connection failed with status: " + status);
                    connected.postValue(false);
                    connectionStatus.postValue("❌ Connection failed");

                    // Clean up failed connection
                    txCharacteristic = null;
                    rxCharacteristic = null;

                    if (bluetoothGatt != null) {
                        bluetoothGatt.close();
                        bluetoothGatt = null;
                    }
                }
            }

            @Override
            public void onMtuChanged(BluetoothGatt gatt, int mtu, int status) {
                Log.d(TAG, "MTU changed: mtu=" + mtu + ", status=" + status);
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    Log.d(TAG, "MTU negotiated successfully: " + mtu + " bytes");
                    connectionStatus.postValue("🔧 Discovering services...");
                } else {
                    Log.e(TAG, "MTU negotiation failed with status: " + status);
                }
                gatt.discoverServices();
            }

            @Override
            public void onServicesDiscovered(BluetoothGatt gatt, int status) {
                Log.d(TAG, "Services discovered: status=" + status);

                if (status == BluetoothGatt.GATT_SUCCESS) {
                    Log.d(TAG, "Service discovery successful");

                    BluetoothGattService service = gatt.getService(SERVICE_UUID);
                    if (service != null) {
                        Log.d(TAG, "LoRa service found!");
                        txCharacteristic = service.getCharacteristic(TX_CHAR_UUID);
                        rxCharacteristic = service.getCharacteristic(RX_CHAR_UUID);

                        Log.d(TAG, "TX characteristic: " + (txCharacteristic != null ? "found" : "NOT FOUND"));
                        Log.d(TAG, "RX characteristic: " + (rxCharacteristic != null ? "found" : "NOT FOUND"));

                        if (txCharacteristic != null && rxCharacteristic != null) {
                            // Enable notifications locally
                            boolean notifySuccess = gatt.setCharacteristicNotification(txCharacteristic, true);
                            Log.d(TAG, "Notification enabled locally on TX: " + notifySuccess);

                            // Write to CCCD descriptor to enable notifications on server side
                            android.bluetooth.BluetoothGattDescriptor descriptor = txCharacteristic
                                    .getDescriptor(CCCD_UUID);
                            if (descriptor != null) {
                                descriptor
                                        .setValue(android.bluetooth.BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                                boolean descriptorWriteSuccess = gatt.writeDescriptor(descriptor);
                                Log.d(TAG, "CCCD descriptor write initiated: " + descriptorWriteSuccess);

                                // DON'T set connected=true here! Wait for onDescriptorWrite callback
                                // to confirm notifications are actually enabled before ESP32 starts sending
                                if (!descriptorWriteSuccess) {
                                    Log.e(TAG, "Failed to initiate descriptor write");
                                    connected.postValue(false);
                                    connectionStatus.postValue("❌ Failed to enable notifications");
                                }
                            } else {
                                Log.e(TAG, "CCCD descriptor not found on TX characteristic!");
                                connected.postValue(false);
                                connectionStatus.postValue("❌ CCCD descriptor missing");
                            }
                        } else {
                            Log.e(TAG, "Characteristics not found!");
                            connected.postValue(false);
                            connectionStatus.postValue("❌ Characteristics missing");
                        }
                    } else {
                        Log.e(TAG, "LoRa service not found! Expected UUID: " + SERVICE_UUID);
                        connected.postValue(false);
                        connectionStatus.postValue("❌ LoRa service not found");
                    }
                } else {
                    Log.e(TAG, "Service discovery failed with status: " + status);
                    connected.postValue(false);
                    connectionStatus.postValue("❌ Service discovery failed");
                }
            }

            @Override
            public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic) {
                if (characteristic.getUuid().equals(TX_CHAR_UUID)) {
                    byte[] data = characteristic.getValue();
                    Log.d(TAG, "Received notification: " + data.length + " bytes");
                    try {
                        Protocol.Message msg = Protocol.Message.deserialize(data);
                        Log.d(TAG, "Deserialized message: " + msg);
                        messageReceived.postValue(msg);
                    } catch (Exception e) {
                        Log.e(TAG, "Failed to deserialize message: " + e.getMessage());
                    }
                }
            }

            @Override
            public void onDescriptorWrite(BluetoothGatt gatt, android.bluetooth.BluetoothGattDescriptor descriptor,
                                          int status) {
                Log.d(TAG, "Descriptor write completed: status=" + status);
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    Log.d(TAG, "Notifications successfully enabled on server side!");

                    // CRITICAL: Only NOW set connected=true
                    // This ensures notifications are fully enabled before ESP32 starts sending buffered messages
                    connected.postValue(true);
                    connectionStatus.postValue("✅ Ready to send!");
                    Log.d(TAG, "Connection FULLY established - ready to receive notifications");
                } else {
                    Log.e(TAG, "Failed to enable notifications on server side, status: " + status);
                    connected.postValue(false);
                    connectionStatus.postValue("❌ Notification setup failed");
                }
            }
        });
    }

    @SuppressLint("MissingPermission")
    public boolean sendMessage(Protocol.Message message) {
        if (message == null) {
            Log.e(TAG, "Cannot send null message");
            return false;
        }

        if (bluetoothGatt == null || rxCharacteristic == null) {
            Log.e(TAG, "Cannot send message: BLE not connected");
            showToast.postValue("Error: Not connected to device");
            return false;
        }

        byte[] data = message.serialize();
        if (data == null || data.length == 0) {
            Log.e(TAG, "Cannot send empty message");
            showToast.postValue("Error: Empty message");
            return false;
        }

        Log.d(TAG, "Sending message: " + data.length + " bytes");

        rxCharacteristic.setValue(data);
        boolean success = bluetoothGatt.writeCharacteristic(rxCharacteristic);
        Log.d(TAG, "Write characteristic result: " + success);
        return success;
    }

    public boolean isConnected() {
        Boolean connectedValue = connected.getValue();
        return connectedValue != null && connectedValue;
    }

    /**
     * Validates that the LiveData connection state matches the actual GATT connection state.
     * Call this when app resumes to ensure UI reflects reality.
     *
     * CRITICAL: Must run synchronously on main thread to prevent race conditions.
     */
    public void validateConnectionState() {
        // If already on main thread, execute immediately. Otherwise post.
        if (android.os.Looper.myLooper() == android.os.Looper.getMainLooper()) {
            validateConnectionStateInternal();
        } else {
            mainHandler.post(this::validateConnectionStateInternal);
        }
    }

    private void validateConnectionStateInternal() {
        // Check actual GATT connection state (must check BluetoothGatt state too)
        boolean gattConnected = false;
        if (bluetoothGatt != null) {
            android.bluetooth.BluetoothManager bluetoothManager =
                (android.bluetooth.BluetoothManager) context.getSystemService(android.content.Context.BLUETOOTH_SERVICE);
            if (bluetoothManager != null && bluetoothGatt.getDevice() != null) {
                int connectionState = bluetoothManager.getConnectionState(
                    bluetoothGatt.getDevice(),
                    android.bluetooth.BluetoothProfile.GATT
                );
                gattConnected = (connectionState == android.bluetooth.BluetoothProfile.STATE_CONNECTED);
            }
        }

        boolean hasCharacteristics = txCharacteristic != null && rxCharacteristic != null;
        boolean actuallyConnected = bluetoothGatt != null && gattConnected && hasCharacteristics;

        Boolean currentLiveDataValue = connected.getValue();
        boolean liveDataSaysConnected = currentLiveDataValue != null && currentLiveDataValue;

        Log.w(TAG, "Validating connection state: " +
                  "LiveData=" + liveDataSaysConnected +
                  ", GATT=" + (bluetoothGatt != null) +
                  ", GATTConnected=" + gattConnected +
                  ", TX=" + (txCharacteristic != null) +
                  ", RX=" + (rxCharacteristic != null) +
                  ", Actual=" + actuallyConnected);

        // ALWAYS update to force observers to fire
        if (actuallyConnected) {
            connected.setValue(true);
            connectionStatus.setValue("✅ Ready to send!");
            Log.d(TAG, "State: CONNECTED");
        } else {
            connected.setValue(false);
            connectionStatus.setValue("❌ Disconnected");
            Log.d(TAG, "State: DISCONNECTED");
        }
    }

    @SuppressLint("MissingPermission")
    public void disconnect() {
        try {
            // Stop scanning if in progress
            stopScan();

            cleanupHandlers();

            if (bluetoothGatt != null) {
                bluetoothGatt.disconnect();
                bluetoothGatt.close();
                bluetoothGatt = null;
            }
            connected.postValue(false);
            isWaitingForLocation = false;
        } finally {
            // Ensure cleanup happens even if exceptions occur
            cleanupHandlers();
        }
    }

    /**
     * Initiates a BLE connection by scanning for the ESP32 device.
     * If already connected, does nothing.
     */
    public void connect() {
        if (isConnected()) {
            Log.d(TAG, "Already connected to BLE device");
            return;
        }

        // Clean up any stale GATT connection before starting new scan
        if (bluetoothGatt != null) {
            Log.d(TAG, "Cleaning up stale GATT connection before reconnect");
            try {
                bluetoothGatt.close();
            } catch (Exception e) {
                Log.e(TAG, "Error closing stale GATT: " + e.getMessage());
            }
            bluetoothGatt = null;
            txCharacteristic = null;
            rxCharacteristic = null;
        }

        Log.d(TAG, "Starting BLE connection process...");
        startScan();
    }
}
