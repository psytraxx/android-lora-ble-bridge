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
import android.util.Log;

import androidx.core.app.ActivityCompat;

import java.util.UUID;

import lora.Protocol;

public class BleManager {

    private static final String TAG = "BleManager";
    private static final String DEVICE_NAME = "ESP32S3-LoRa";
    private static final UUID SERVICE_UUID = UUID.fromString("00001234-0000-1000-8000-00805F9B34FB");
    private static final UUID TX_CHAR_UUID = UUID.fromString("00005678-0000-1000-8000-00805F9B34FB");
    private static final UUID RX_CHAR_UUID = UUID.fromString("00005679-0000-1000-8000-00805F9B34FB");
    private static final UUID CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private final Context context;
    private final BleCallback callback;
    private BluetoothAdapter bluetoothAdapter;
    private BluetoothLeScanner bluetoothLeScanner;
    private BluetoothGatt bluetoothGatt;
    private BluetoothGattCharacteristic txCharacteristic;
    private BluetoothGattCharacteristic rxCharacteristic;
    private boolean isConnected = false;

    public BleManager(Context context, BleCallback callback) {
        this.context = context;
        this.callback = callback;
        initializeBluetooth();
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

    @SuppressLint("MissingPermission")
    public void startScan() {
        if (!hasPermissions()) {
            callback.onConnectionStatusChanged("❌ BT permissions missing");
            return;
        }

        if (!isBluetoothEnabled()) {
            callback.onConnectionStatusChanged("❌ BT not enabled");
            return;
        }

        Log.d(TAG, "Starting BLE scan for device: " + DEVICE_NAME);
        callback.onConnectionStatusChanged("🔍 Scanning...");

        bluetoothLeScanner.startScan(new ScanCallback() {
            @Override
            public void onScanResult(int callbackType, ScanResult result) {
                BluetoothDevice device = result.getDevice();
                String deviceName = device.getName();
                Log.d(TAG, "Found BLE device: " + deviceName + " (" + device.getAddress() + ")");

                if (DEVICE_NAME.equals(deviceName)) {
                    Log.d(TAG, "Target device found! Connecting...");
                    callback.onConnectionStatusChanged("📡 Connecting...");
                    bluetoothLeScanner.stopScan(this);
                    connectToDevice(device);
                }
            }

            @Override
            public void onScanFailed(int errorCode) {
                Log.e(TAG, "BLE scan failed with error code: " + errorCode);
                callback.onConnectionStatusChanged("❌ Scan failed (error " + errorCode + ")");
            }
        });
    }

    @SuppressLint("MissingPermission")
    private void connectToDevice(BluetoothDevice device) {
        Log.d(TAG, "Connecting to device: " + device.getAddress());
        bluetoothGatt = device.connectGatt(context, false, new BluetoothGattCallback() {
            @Override
            public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
                Log.d(TAG, "Connection state changed: status=" + status + ", newState=" + newState);

                if (newState == BluetoothGatt.STATE_CONNECTED) {
                    Log.d(TAG, "Connected! Requesting MTU...");
                    callback.onConnectionStatusChanged("🔗 Negotiating...");
                    gatt.requestMtu(512);
                } else if (newState == BluetoothGatt.STATE_DISCONNECTED) {
                    Log.d(TAG, "Disconnected from device");
                    isConnected = false;
                    callback.onDisconnected();
                    callback.onConnectionStatusChanged("❌ Disconnected");
                    // Retry scan after disconnect
                    new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(() -> startScan(), 2000);
                }
            }

            @Override
            public void onMtuChanged(BluetoothGatt gatt, int mtu, int status) {
                Log.d(TAG, "MTU changed: mtu=" + mtu + ", status=" + status);
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    Log.d(TAG, "MTU negotiated successfully: " + mtu + " bytes");
                    callback.onConnectionStatusChanged("🔧 Discovering services...");
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

                            isConnected = true;
                            callback.onConnected();
                            callback.onConnectionStatusChanged("✅ Ready to send!");
                        } else {
                            Log.e(TAG, "Characteristics not found!");
                            callback.onConnectionStatusChanged("❌ Characteristics missing");
                        }
                    } else {
                        Log.e(TAG, "LoRa service not found! Expected UUID: " + SERVICE_UUID);
                        callback.onConnectionStatusChanged("❌ LoRa service not found");
                    }
                } else {
                    Log.e(TAG, "Service discovery failed with status: " + status);
                    callback.onConnectionStatusChanged("❌ Service discovery failed");
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
                        callback.onMessageReceived(msg);
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
        if (!isConnected || bluetoothGatt == null || rxCharacteristic == null) {
            Log.e(TAG, "Cannot send: not connected");
            return false;
        }

        byte[] data = message.serialize();
        Log.d(TAG, "Sending message: " + data.length + " bytes");

        rxCharacteristic.setValue(data);
        boolean success = bluetoothGatt.writeCharacteristic(rxCharacteristic);
        Log.d(TAG, "Write characteristic result: " + success);
        return success;
    }

    public boolean isConnected() {
        return isConnected;
    }

    @SuppressLint("MissingPermission")
    public void disconnect() {
        if (bluetoothGatt != null) {
            bluetoothGatt.disconnect();
            bluetoothGatt.close();
            bluetoothGatt = null;
        }
        isConnected = false;
    }

    public interface BleCallback {
        void onConnectionStatusChanged(String status);

        void onConnected();

        void onDisconnected();

        void onMessageReceived(Protocol.Message message);
    }
}