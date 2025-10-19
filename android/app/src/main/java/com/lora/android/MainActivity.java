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

import lora.Protocol;

import java.util.UUID;

public class MainActivity extends AppCompatActivity {

    private static final int REQUEST_PERMISSIONS = 1;
    private static final int MAX_TEXT_LENGTH = 50; // Maximum text length for optimal LoRa range
    private static final String DEVICE_NAME = "ESP32-LoRa";
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

    private LocationManager locationManager;
    private byte seqCounter = 0;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        messageEditText = findViewById(R.id.messageEditText);
        Button sendButton = findViewById(R.id.sendButton);
        receivedTextView = findViewById(R.id.receivedTextView);
        gpsTextView = findViewById(R.id.gpsTextView);
        charCountTextView = findViewById(R.id.charCountTextView);

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
            Toast.makeText(this, "Bluetooth permissions required", Toast.LENGTH_SHORT).show();
            return;
        }

        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled()) {
            Toast.makeText(this, "Bluetooth not enabled", Toast.LENGTH_SHORT).show();
            return;
        }

        bluetoothLeScanner.startScan(new ScanCallback() {
            @Override
            public void onScanResult(int callbackType, ScanResult result) {
                BluetoothDevice device = result.getDevice();
                if (DEVICE_NAME.equals(device.getName())) {
                    bluetoothLeScanner.stopScan(this);
                    connectToDevice(device);
                }
            }
        });
    }

    @SuppressLint("MissingPermission")
    private void connectToDevice(BluetoothDevice device) {
        bluetoothGatt = device.connectGatt(this, false, new BluetoothGattCallback() {
            @Override
            public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
                if (newState == BluetoothGatt.STATE_CONNECTED) {
                    // Request larger MTU to support messages up to 266 bytes
                    // Default MTU is 23 bytes (20 usable), we request 512 to be safe
                    gatt.requestMtu(512);
                }
            }

            @Override
            public void onMtuChanged(BluetoothGatt gatt, int mtu, int status) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    runOnUiThread(() -> Toast.makeText(MainActivity.this,
                            "MTU negotiated: " + mtu + " bytes", Toast.LENGTH_SHORT).show());
                }
                // After MTU negotiation, discover services
                gatt.discoverServices();
            }

            @Override
            public void onServicesDiscovered(BluetoothGatt gatt, int status) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    BluetoothGattService service = gatt.getService(SERVICE_UUID);
                    if (service != null) {
                        txCharacteristic = service.getCharacteristic(TX_CHAR_UUID);
                        rxCharacteristic = service.getCharacteristic(RX_CHAR_UUID);
                        gatt.setCharacteristicNotification(rxCharacteristic, true);
                    }
                }
            }

            @Override
            public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic) {
                if (characteristic.getUuid().equals(RX_CHAR_UUID)) {
                    byte[] data = characteristic.getValue();
                    try {
                        Protocol.Message msg = Protocol.Message.deserialize(data);
                        if (msg instanceof Protocol.TextMessage) {
                            Protocol.TextMessage textMsg = (Protocol.TextMessage) msg;
                            runOnUiThread(() -> receivedTextView.setText(
                                    "Received: " + textMsg.text));
                        } else if (msg instanceof Protocol.GpsMessage) {
                            Protocol.GpsMessage gpsMsg = (Protocol.GpsMessage) msg;
                            runOnUiThread(() -> receivedTextView.setText(
                                    "GPS: Lat=" + gpsMsg.lat + " Lon=" + gpsMsg.lon));
                        } else if (msg instanceof Protocol.AckMessage) {
                            runOnUiThread(
                                    () -> Toast.makeText(MainActivity.this, "ACK received", Toast.LENGTH_SHORT).show());
                        }
                    } catch (Exception e) {
                        // Invalid data
                    }
                }
            }
        });
    }

    @SuppressLint("MissingPermission")
    private void sendMessage() {
        if (bluetoothGatt == null || txCharacteristic == null) {
            Toast.makeText(this, "Not connected", Toast.LENGTH_SHORT).show();
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
            // Send text message first
            Protocol.TextMessage textMsg = new Protocol.TextMessage(seqCounter++, text);
            byte[] textData = textMsg.serialize();

            txCharacteristic.setValue(textData);
            bluetoothGatt.writeCharacteristic(txCharacteristic);

            // Send GPS message only if GPS is enabled and available
            if (location != null) {
                int lat = (int) (location.getLatitude() * 1_000_000);
                int lon = (int) (location.getLongitude() * 1_000_000);

                // Small delay to ensure messages are sent in order
                new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(() -> {
                    // Send GPS message second
                    Protocol.GpsMessage gpsMsg = new Protocol.GpsMessage(seqCounter++, lat, lon);
                    byte[] gpsData = gpsMsg.serialize();

                    txCharacteristic.setValue(gpsData);
                    bluetoothGatt.writeCharacteristic(txCharacteristic);

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
}