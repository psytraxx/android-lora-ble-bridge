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
    private static final long LOCATION_CHECK_INTERVAL_MS = 3000; // Check every 3 seconds
    private static final long RECONNECT_DELAY_MS = 30000; // 30 seconds between reconnect attempts
    private static final long SCAN_TIMEOUT_MS = 5000; // 5 seconds scan timeout

    private final Context context;
    private BluetoothAdapter bluetoothAdapter;
    private BluetoothLeScanner bluetoothLeScanner;
    private BluetoothGatt bluetoothGatt;
    private BluetoothGattCharacteristic txCharacteristic;
    private BluetoothGattCharacteristic rxCharacteristic;
    private boolean isWaitingForLocation = false;
    private boolean isScanning = false;
    private ScanCallback currentScanCallback = null;
    private final android.os.Handler locationCheckHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    private final android.os.Handler mainHandler = new android.os.Handler(android.os.Looper.getMainLooper());

    // LiveData for state changes
    private final MutableLiveData<String> connectionStatus = new MutableLiveData<>();
    private final MutableLiveData<Protocol.Message> messageReceived = new MutableLiveData<>();
    private final MutableLiveData<String> showToast = new MutableLiveData<>();
    private final MutableLiveData<Boolean> connected = new MutableLiveData<>();

    public BleManager(Context context) {
        this.context = context;
        initializeBluetooth();
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

            // Start checking periodically for location services to be enabled
            if (!isWaitingForLocation) {
                isWaitingForLocation = true;
                startLocationCheck();
            }
            return;
        }

        // Location is enabled, stop waiting
        isWaitingForLocation = false;
        locationCheckHandler.removeCallbacksAndMessages(null);

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
                // Retry after delay
                mainHandler.postDelayed(() -> startScan(), RECONNECT_DELAY_MS);
            }
        };

        bluetoothLeScanner.startScan(currentScanCallback);

        // Set scan timeout
        mainHandler.postDelayed(() -> {
            if (isScanning) {
                Log.w(TAG, "Scan timeout - device not found");
                stopScan();
                connectionStatus.postValue("❌ Device not found (retrying in " + (RECONNECT_DELAY_MS / 1000) + "s)");
                // Retry scan after delay
                mainHandler.postDelayed(() -> startScan(), RECONNECT_DELAY_MS);
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

    private void startLocationCheck() {
        locationCheckHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (isWaitingForLocation && !isConnected()) {
                    if (isLocationEnabled()) {
                        Log.d(TAG, "Location services now enabled! Retrying BLE scan...");
                        showToast.postValue("Location enabled! Scanning for device...");
                        startScan();
                    } else {
                        // Keep checking
                        locationCheckHandler.postDelayed(this, LOCATION_CHECK_INTERVAL_MS);
                    }
                }
            }
        }, LOCATION_CHECK_INTERVAL_MS);
    }

    @SuppressLint("MissingPermission")
    private void connectToDevice(BluetoothDevice device) {
        Log.d(TAG, "Connecting to device: " + device.getAddress());
        bluetoothGatt = device.connectGatt(context, false, new BluetoothGattCallback() {
            @Override
            public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
                Log.d(TAG, "Connection state changed: status=" + status + ", newState=" + newState);

                if (newState == BluetoothGatt.STATE_CONNECTED) {
                    connected.postValue(true);
                    Log.d(TAG, "Connected! Requesting MTU...");
                    connectionStatus.postValue("🔗 Negotiating...");
                    gatt.requestMtu(512);
                } else if (newState == BluetoothGatt.STATE_DISCONNECTED) {
                    connected.postValue(false);
                    connectionStatus.postValue("❌ Disconnected (reconnecting in " + (RECONNECT_DELAY_MS / 1000) + "s)");
                    Log.d(TAG, "Disconnected. Will retry in " + (RECONNECT_DELAY_MS / 1000) + " seconds");
                    
                    // Retry indefinitely with 30 second delay
                    mainHandler.postDelayed(() -> {
                        Log.d(TAG, "Attempting reconnection...");
                        startScan();
                    }, RECONNECT_DELAY_MS);
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
                            } else {
                                Log.e(TAG, "CCCD descriptor not found on TX characteristic!");
                            }

                            connected.postValue(true);
                            connectionStatus.postValue("✅ Ready to send!");
                        } else {
                            Log.e(TAG, "Characteristics not found!");
                            connectionStatus.postValue("❌ Characteristics missing");
                        }
                    } else {
                        Log.e(TAG, "LoRa service not found! Expected UUID: " + SERVICE_UUID);
                        connectionStatus.postValue("❌ LoRa service not found");
                    }
                } else {
                    Log.e(TAG, "Service discovery failed with status: " + status);
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
                } else {
                    Log.e(TAG, "Failed to enable notifications on server side, status: " + status);
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

    @SuppressLint("MissingPermission")
    public void disconnect() {
        // Stop scanning if in progress
        stopScan();
        
        // Cancel any pending reconnect attempts
        mainHandler.removeCallbacksAndMessages(null);
        locationCheckHandler.removeCallbacksAndMessages(null);
        
        if (bluetoothGatt != null) {
            bluetoothGatt.disconnect();
            bluetoothGatt.close();
            bluetoothGatt = null;
        }
        connected.postValue(false);
        isWaitingForLocation = false;
    }
}