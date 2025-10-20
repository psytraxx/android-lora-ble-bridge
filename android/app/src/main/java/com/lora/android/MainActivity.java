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
import android.location.Location;
import android.location.LocationManager;
import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.util.UUID;

import lora.Protocol;

public class MainActivity extends AppCompatActivity {

    private static final int REQUEST_PERMISSIONS = 1;
    private static final int MAX_TEXT_LENGTH = 50; // Maximum text length for optimal LoRa range
    private static final String DEVICE_NAME = "ESP32S3-LoRa"; // Fixed: was "ESP32-LoRa", ESP32 advertises as "ESP32S3-LoRa"
    // Bluetooth Base UUID: 0000xxxx-0000-1000-8000-00805F9B34FB
    // Service UUID: 0x1234 -> 00001234-0000-1000-8000-00805F9B34FB
    private static final UUID SERVICE_UUID = UUID.fromString("00001234-0000-1000-8000-00805F9B34FB");
    // TX Characteristic UUID: 0x5678 -> 00005678-0000-1000-8000-00805F9B34FB
    private static final UUID TX_CHAR_UUID = UUID.fromString("00005678-0000-1000-8000-00805F9B34FB");
    // RX Characteristic UUID: 0x5679 -> 00005679-0000-1000-8000-00805F9B34FB
    private static final UUID RX_CHAR_UUID = UUID.fromString("00005679-0000-1000-8000-00805F9B34FB");

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothLeScanner bluetoothLeScanner;
    private BluetoothGatt bluetoothGatt;
    private BluetoothGattCharacteristic txCharacteristic;
    private BluetoothGattCharacteristic rxCharacteristic;

    private EditText messageEditText;
    private TextView receivedTextView;
    private TextView gpsTextView;
    private TextView charCountTextView;
    private TextView connectionStatusTextView;

    private LocationManager locationManager;
    private byte seqCounter = 0;
    private boolean isConnected = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        messageEditText = findViewById(R.id.messageEditText);
        Button sendButton = findViewById(R.id.sendButton);
        receivedTextView = findViewById(R.id.receivedTextView);
        gpsTextView = findViewById(R.id.gpsTextView);
        charCountTextView = findViewById(R.id.charCountTextView);
        connectionStatusTextView = findViewById(R.id.connectionStatusTextView);

        BluetoothManager bluetoothManager = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        bluetoothAdapter = bluetoothManager.getAdapter();
        bluetoothLeScanner = bluetoothAdapter.getBluetoothLeScanner();
        locationManager = (LocationManager) getSystemService(Context.LOCATION_SERVICE);

        checkPermissions();

        sendButton.setOnClickListener(v -> sendMessage());

        // Add text watcher to update character count
        messageEditText.addTextChangedListener(new android.text.TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                updateCharCount(s.toString());
            }

            @Override
            public void afterTextChanged(android.text.Editable s) {
            }
        });

        updateGps();
        updateCharCount(""); // Initialize counter
        updateConnectionStatus("Initializing...");
    }

    private void checkPermissions() {
        String[] permissions = {
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.ACCESS_FINE_LOCATION
        };

        boolean allGranted = true;
        for (String perm : permissions) {
            if (ContextCompat.checkSelfPermission(this, perm) != PackageManager.PERMISSION_GRANTED) {
                allGranted = false;
                break;
            }
        }

        if (!allGranted) {
            ActivityCompat.requestPermissions(this, permissions, REQUEST_PERMISSIONS);
        } else {
            startBleScan();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
                                           @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_PERMISSIONS) {
            boolean allGranted = true;
            for (int result : grantResults) {
                if (result != PackageManager.PERMISSION_GRANTED) {
                    allGranted = false;
                    break;
                }
            }
            if (allGranted) {
                startBleScan();
            } else {
                Toast.makeText(this, "Permissions required", Toast.LENGTH_SHORT).show();
            }
        }
    }

    @SuppressLint("MissingPermission")
    private void startBleScan() {
        if (ActivityCompat.checkSelfPermission(this,
                Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED ||
                ActivityCompat.checkSelfPermission(this,
                        Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            updateConnectionStatus("❌ Bluetooth permissions missing");
            Toast.makeText(this, "Bluetooth permissions required", Toast.LENGTH_SHORT).show();
            return;
        }

        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled()) {
            updateConnectionStatus("❌ Bluetooth not enabled");
            Toast.makeText(this, "Bluetooth not enabled", Toast.LENGTH_SHORT).show();
            return;
        }

        android.util.Log.d("LoRaApp", "Starting BLE scan for device: " + DEVICE_NAME);
        updateConnectionStatus("🔍 Scanning for " + DEVICE_NAME + "...");

        bluetoothLeScanner.startScan(new ScanCallback() {
            @Override
            public void onScanResult(int callbackType, ScanResult result) {
                BluetoothDevice device = result.getDevice();
                String deviceName = device.getName();
                android.util.Log.d("LoRaApp", "Found BLE device: " + deviceName + " (" + device.getAddress() + ")");
                
                if (DEVICE_NAME.equals(deviceName)) {
                    android.util.Log.d("LoRaApp", "Target device found! Connecting...");
                    updateConnectionStatus("📡 Found " + DEVICE_NAME + ", connecting...");
                    bluetoothLeScanner.stopScan(this);
                    connectToDevice(device);
                }
            }

            @Override
            public void onScanFailed(int errorCode) {
                android.util.Log.e("LoRaApp", "BLE scan failed with error code: " + errorCode);
                updateConnectionStatus("❌ Scan failed (error " + errorCode + ")");
            }
        });
    }

    @SuppressLint("MissingPermission")
    private void connectToDevice(BluetoothDevice device) {
        android.util.Log.d("LoRaApp", "Connecting to device: " + device.getAddress());
        bluetoothGatt = device.connectGatt(this, false, new BluetoothGattCallback() {
            @Override
            public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
                android.util.Log.d("LoRaApp", "Connection state changed: status=" + status + ", newState=" + newState);
                
                if (newState == BluetoothGatt.STATE_CONNECTED) {
                    android.util.Log.d("LoRaApp", "Connected! Requesting MTU...");
                    updateConnectionStatus("🔗 Connected, negotiating MTU...");
                    // Request larger MTU to support messages up to 266 bytes
                    // Default MTU is 23 bytes (20 usable), we request 512 to be safe
                    gatt.requestMtu(512);
                } else if (newState == BluetoothGatt.STATE_DISCONNECTED) {
                    android.util.Log.d("LoRaApp", "Disconnected from device");
                    isConnected = false;
                    updateConnectionStatus("❌ Disconnected - Retrying scan...");
                    // Retry scan after disconnect
                    new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(() -> {
                        startBleScan();
                    }, 2000); // Wait 2 seconds before retrying
                }
            }

            @Override
            public void onMtuChanged(BluetoothGatt gatt, int mtu, int status) {
                android.util.Log.d("LoRaApp", "MTU changed: mtu=" + mtu + ", status=" + status);
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    android.util.Log.d("LoRaApp", "MTU negotiated successfully: " + mtu + " bytes");
                    updateConnectionStatus("🔧 MTU=" + mtu + "B, discovering services...");
                } else {
                    android.util.Log.e("LoRaApp", "MTU negotiation failed with status: " + status);
                }
                // After MTU negotiation, discover services
                android.util.Log.d("LoRaApp", "Discovering GATT services...");
                gatt.discoverServices();
            }

            @Override
            public void onServicesDiscovered(BluetoothGatt gatt, int status) {
                android.util.Log.d("LoRaApp", "Services discovered: status=" + status);
                
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    android.util.Log.d("LoRaApp", "Service discovery successful");
                    
                    // Log all discovered services for debugging
                    for (BluetoothGattService svc : gatt.getServices()) {
                        android.util.Log.d("LoRaApp", "Found service: " + svc.getUuid());
                    }
                    
                    BluetoothGattService service = gatt.getService(SERVICE_UUID);
                    if (service != null) {
                        android.util.Log.d("LoRaApp", "LoRa service found!");
                        txCharacteristic = service.getCharacteristic(TX_CHAR_UUID); // ESP32 TX = Android RX (receive notifications)
                        rxCharacteristic = service.getCharacteristic(RX_CHAR_UUID); // ESP32 RX = Android TX (write to this)
                        
                        android.util.Log.d("LoRaApp", "TX characteristic (ESP32->Android): " + (txCharacteristic != null ? "found" : "NOT FOUND"));
                        android.util.Log.d("LoRaApp", "RX characteristic (Android->ESP32): " + (rxCharacteristic != null ? "found" : "NOT FOUND"));
                        
                        if (txCharacteristic != null && rxCharacteristic != null) {
                            // Enable notifications on TX characteristic (to receive messages FROM ESP32)
                            boolean notifySuccess = gatt.setCharacteristicNotification(txCharacteristic, true);
                            android.util.Log.d("LoRaApp", "Notification enabled on TX: " + notifySuccess);
                            isConnected = true;
                            updateConnectionStatus("✅ Connected - Ready to send!");
                        } else {
                            android.util.Log.e("LoRaApp", "Characteristics not found!");
                            updateConnectionStatus("❌ Characteristics missing");
                        }
                    } else {
                        android.util.Log.e("LoRaApp", "LoRa service not found! Expected UUID: " + SERVICE_UUID);
                        updateConnectionStatus("❌ LoRa service not found");
                    }
                } else {
                    android.util.Log.e("LoRaApp", "Service discovery failed with status: " + status);
                    updateConnectionStatus("❌ Service discovery failed");
                }
            }

            @Override
            public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic) {
                // Receive notifications from ESP32's TX characteristic
                if (characteristic.getUuid().equals(TX_CHAR_UUID)) {
                    byte[] data = characteristic.getValue();
                    android.util.Log.d("LoRaApp", "Received notification: " + data.length + " bytes");
                    try {
                        Protocol.Message msg = Protocol.Message.deserialize(data);
                        android.util.Log.d("LoRaApp", "Deserialized message: " + msg);
                        
                        if (msg instanceof Protocol.TextMessage) {
                            Protocol.TextMessage textMsg = (Protocol.TextMessage) msg;
                            android.util.Log.d("LoRaApp", "Text message received: " + textMsg.text);
                            runOnUiThread(() -> receivedTextView.setText(
                                    "Received: " + textMsg.text));
                        } else if (msg instanceof Protocol.GpsMessage) {
                            Protocol.GpsMessage gpsMsg = (Protocol.GpsMessage) msg;
                            android.util.Log.d("LoRaApp", "GPS message received: lat=" + gpsMsg.lat + ", lon=" + gpsMsg.lon);
                            runOnUiThread(() -> receivedTextView.setText(
                                    "GPS: Lat=" + gpsMsg.lat + " Lon=" + gpsMsg.lon));
                        } else if (msg instanceof Protocol.AckMessage) {
                            android.util.Log.d("LoRaApp", "ACK received for seq: " + ((Protocol.AckMessage) msg).seq);
                            runOnUiThread(
                                    () -> Toast.makeText(MainActivity.this, "ACK received", Toast.LENGTH_SHORT).show());
                        }
                    } catch (Exception e) {
                        android.util.Log.e("LoRaApp", "Failed to deserialize message: " + e.getMessage());
                    }
                }
            }
        });
    }

    @SuppressLint("MissingPermission")
    private void sendMessage() {
        android.util.Log.d("LoRaApp", "Send message clicked - isConnected=" + isConnected + 
                           ", gatt=" + (bluetoothGatt != null) + 
                           ", rxChar=" + (rxCharacteristic != null));
        
        if (!isConnected || bluetoothGatt == null || rxCharacteristic == null) {
            String reason = !isConnected ? "Not connected" : 
                           bluetoothGatt == null ? "GATT null" : "RX characteristic null";
            android.util.Log.e("LoRaApp", "Cannot send: " + reason);
            Toast.makeText(this, "Not connected - Check status above", Toast.LENGTH_LONG).show();
            return;
        }

        String text = messageEditText.getText().toString();

        // Enforce maximum text length (should be caught by UI, but double-check)
        if (text.length() > MAX_TEXT_LENGTH) {
            text = text.substring(0, MAX_TEXT_LENGTH);
            Toast.makeText(this, "Message truncated to " + MAX_TEXT_LENGTH + " chars", Toast.LENGTH_SHORT).show();
        }

        // Validate all characters are supported
        if (!Protocol.isTextSupported(text)) {
            Toast.makeText(this, "Invalid characters! Use only A-Z, 0-9, space, and punctuation.", Toast.LENGTH_LONG)
                    .show();
            return;
        }

        Location location = getLastKnownLocation();

        try {
            // Send text message first - WRITE TO RX CHARACTERISTIC (Android->ESP32)
            Protocol.TextMessage textMsg = new Protocol.TextMessage(seqCounter++, text);
            byte[] textData = textMsg.serialize();
            
            android.util.Log.d("LoRaApp", "Sending text message to RX characteristic: " + text + " (" + textData.length + " bytes)");

            rxCharacteristic.setValue(textData);
            boolean writeSuccess = bluetoothGatt.writeCharacteristic(rxCharacteristic);
            android.util.Log.d("LoRaApp", "Write characteristic result: " + writeSuccess);

            // Send GPS message only if GPS is enabled and available
            if (location != null) {
                int lat = (int) (location.getLatitude() * 1_000_000);
                int lon = (int) (location.getLongitude() * 1_000_000);

                // Small delay to ensure messages are sent in order
                new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(() -> {
                    // Send GPS message second - WRITE TO RX CHARACTERISTIC (Android->ESP32)
                    Protocol.GpsMessage gpsMsg = new Protocol.GpsMessage(seqCounter++, lat, lon);
                    byte[] gpsData = gpsMsg.serialize();
                    
                    android.util.Log.d("LoRaApp", "Sending GPS message to RX characteristic: lat=" + lat + ", lon=" + lon + " (" + gpsData.length + " bytes)");

                    rxCharacteristic.setValue(gpsData);
                    boolean gpsWriteSuccess = bluetoothGatt.writeCharacteristic(rxCharacteristic);
                    android.util.Log.d("LoRaApp", "GPS write characteristic result: " + gpsWriteSuccess);

                    Toast.makeText(this, "Sent text (" + textData.length + "B) + GPS (" + gpsData.length + "B)",
                            Toast.LENGTH_SHORT).show();
                }, 100); // 100ms delay between messages
            } else {
                // No GPS available - text only
                Toast.makeText(this, "Sent text only (" + textData.length + "B) - No GPS",
                        Toast.LENGTH_SHORT).show();
            }

            messageEditText.setText(""); // Clear input after sending
        } catch (IllegalArgumentException e) {
            Toast.makeText(this, "Error: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private void updateCharCount(String text) {
        int charCount = text.length();
        int packedBytes = Protocol.calculatePackedSize(text);
        int totalMessageSize = 12 + packedBytes; // 12 byte header + packed text

        String countText = charCount + "/" + MAX_TEXT_LENGTH + " chars (" + totalMessageSize + " bytes)";
        charCountTextView.setText(countText);

        // Change color if approaching limit
        if (charCount >= MAX_TEXT_LENGTH) {
            charCountTextView.setTextColor(0xFFFF0000); // Red
        } else if (charCount >= MAX_TEXT_LENGTH * 0.9) {
            charCountTextView.setTextColor(0xFFFF6600); // Orange
        } else {
            charCountTextView.setTextColor(0xFF666666); // Gray
        }
    }

    private Location getLastKnownLocation() {
        if (ActivityCompat.checkSelfPermission(this,
                Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            return null;
        }
        return locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER);
    }

    private void updateGps() {
        Location location = getLastKnownLocation();
        if (location != null) {
            gpsTextView.setText("GPS: " + location.getLatitude() + ", " + location.getLongitude());
        }
    }

    private void updateConnectionStatus(String status) {
        android.util.Log.d("LoRaApp", "Connection status: " + status);
        runOnUiThread(() -> {
            if (connectionStatusTextView != null) {
                connectionStatusTextView.setText(status);
            }
        });
    }
}