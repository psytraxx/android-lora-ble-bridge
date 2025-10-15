package com.lora.android;

import android.Manifest;
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
    private static final String DEVICE_NAME = "ESP32-LoRa";
    private static final UUID SERVICE_UUID = UUID.fromString("12340000-0000-0000-0000-000000000000");
    private static final UUID TX_CHAR_UUID = UUID.fromString("56780000-0000-0000-0000-000000000000");
    private static final UUID RX_CHAR_UUID = UUID.fromString("56790000-0000-0000-0000-000000000000");

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothLeScanner bluetoothLeScanner;
    private BluetoothGatt bluetoothGatt;
    private BluetoothGattCharacteristic txCharacteristic;
    private BluetoothGattCharacteristic rxCharacteristic;

    private EditText messageEditText;
    private Button sendButton;
    private TextView receivedTextView;
    private TextView gpsTextView;

    private LocationManager locationManager;
    private byte seqCounter = 0;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        messageEditText = findViewById(R.id.messageEditText);
        sendButton = findViewById(R.id.sendButton);
        receivedTextView = findViewById(R.id.receivedTextView);
        gpsTextView = findViewById(R.id.gpsTextView);

        BluetoothManager bluetoothManager = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        bluetoothAdapter = bluetoothManager.getAdapter();
        bluetoothLeScanner = bluetoothAdapter.getBluetoothLeScanner();
        locationManager = (LocationManager) getSystemService(Context.LOCATION_SERVICE);

        checkPermissions();

        sendButton.setOnClickListener(v -> sendMessage());

        updateGps();
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
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
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

    private void startBleScan() {
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

    private void connectToDevice(BluetoothDevice device) {
        bluetoothGatt = device.connectGatt(this, false, new BluetoothGattCallback() {
            @Override
            public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
                if (newState == BluetoothGatt.STATE_CONNECTED) {
                    gatt.discoverServices();
                }
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
                        if (msg instanceof Protocol.Message.Data) {
                            Protocol.DataMessage dataMsg = ((Protocol.Message.Data) msg).dataMessage;
                            runOnUiThread(() -> receivedTextView.setText("Received: " + dataMsg.text + " Lat: " + dataMsg.lat + " Lon: " + dataMsg.lon));
                        } else if (msg instanceof Protocol.Message.Ack) {
                            runOnUiThread(() -> Toast.makeText(MainActivity.this, "ACK received", Toast.LENGTH_SHORT).show());
                        }
                    } catch (Exception e) {
                        // Invalid data
                    }
                }
            }
        });
    }

    private void sendMessage() {
        if (bluetoothGatt == null || txCharacteristic == null) {
            Toast.makeText(this, "Not connected", Toast.LENGTH_SHORT).show();
            return;
        }

        String text = messageEditText.getText().toString();
        Location location = getLastKnownLocation();
        if (location == null) {
            Toast.makeText(this, "No GPS", Toast.LENGTH_SHORT).show();
            return;
        }

        int lat = (int) (location.getLatitude() * 1_000_000);
        int lon = (int) (location.getLongitude() * 1_000_000);

        Protocol.DataMessage dataMsg = new Protocol.DataMessage(seqCounter++, text, lat, lon);
        Protocol.Message msg = new Protocol.Message.Data(dataMsg);
        byte[] data = msg.serialize();

        txCharacteristic.setValue(data);
        bluetoothGatt.writeCharacteristic(txCharacteristic);
    }

    private Location getLastKnownLocation() {
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
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