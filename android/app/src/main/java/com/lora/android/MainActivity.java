package com.lora.android;

import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.lifecycle.ViewModelProvider;
import androidx.recyclerview.widget.LinearLayoutManager;

import com.lora.android.databinding.ActivityMainBinding;

import lora.Protocol;

public class MainActivity extends AppCompatActivity {

    private static final int REQUEST_PERMISSIONS = 1;
    private static final long GPS_UPDATE_INTERVAL_MS = 5000; // 5 seconds
    private static final double CHAR_COUNT_WARNING_THRESHOLD = 0.9; // 90% of max

    private ActivityMainBinding binding;

    private MessageAdapter messageAdapter;

    private MessageViewModel messageViewModel;
    private BleManager bleManager;
    private GpsManager gpsManager;

    private android.os.Handler gpsHandler;
    private Runnable gpsUpdateRunnable;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        // Ensure status bar is visible and icons are dark
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.show(WindowInsets.Type.statusBars());
                controller.setSystemBarsAppearance(WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS,
                        WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS);
            }
        } else {
            View decorView = getWindow().getDecorView();
            decorView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
        }

        // Set up Action Bar title
        if (getSupportActionBar() != null) {
            getSupportActionBar().setTitle("LoRa Chat");
        }

        // Initialize managers
        messageAdapter = new MessageAdapter();

        // Set scroll callback to auto-scroll when messages are added
        messageAdapter.setScrollCallback(() -> binding.messagesRecyclerView.post(() -> {
            if (messageAdapter.getItemCount() > 0) {
                binding.messagesRecyclerView.smoothScrollToPosition(messageAdapter.getItemCount() - 1);
            }
        }));

        messageViewModel = new ViewModelProvider(this).get(MessageViewModel.class);
        bleManager = new BleManager(this);
        gpsManager = new GpsManager(this);

        messageViewModel.setManagers(bleManager, gpsManager, messageAdapter);

        // Set up RecyclerView
        binding.messagesRecyclerView.setLayoutManager(new LinearLayoutManager(this));
        binding.messagesRecyclerView.setAdapter(messageAdapter);

        // Observe ViewModel
        messageViewModel.getGpsDisplay().observe(this, gps -> binding.gpsTextView.setText(gps));
        messageViewModel.getShowToast().observe(this, message -> {
            if (message != null) {
                Toast.makeText(MainActivity.this, message, Toast.LENGTH_LONG).show();
            }
        });

        // Observe BLE connection state for send button and status
        bleManager.getConnectionStatus().observe(this, status -> binding.connectionStatusTextView.setText(status));
        bleManager.getConnected().observe(this,
                connected -> binding.sendButton.setEnabled(connected != null ? connected : false));

        // Add click listener for reconnect functionality
        binding.connectionStatusTextView.setOnClickListener(v -> {
            String currentStatus = binding.connectionStatusTextView.getText().toString();
            if (currentStatus.contains("Tap here to reconnect")) {
                bleManager.connect();
            }
        });

        checkPermissions();

        binding.sendButton.setOnClickListener(v -> {
            String messageText = binding.messageEditText.getText().toString();
            if (!messageText.isEmpty()) {
                messageViewModel.sendMessage(messageText);
                binding.messageEditText.setText("");
            }
        });
        binding.sendButton.setEnabled(false); // Disabled until connected

        // Add text watcher to update character count
        binding.messageEditText.addTextChangedListener(new android.text.TextWatcher() {
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

        binding.messageEditText.setOnEditorActionListener((v, actionId, event) -> {
            android.util.Log.d("LoRaApp", "EditorAction - actionId: " + actionId + ", event: " + event);

            // Handle any action - Done, Send, Unspecified, etc.
            if (actionId == EditorInfo.IME_ACTION_DONE ||
                    actionId == EditorInfo.IME_ACTION_SEND ||
                    actionId == EditorInfo.IME_ACTION_GO ||
                    actionId == EditorInfo.IME_NULL) {
                dismissKeyboard();
                return true;
            }
            return false;
        });

        messageViewModel.updateGps();
        updateCharCount(""); // Initialize counter

        startGpsPeriodicUpdates();
    }

    private void checkPermissions() {
        if (!PermissionHelper.hasAllPermissions(this)) {
            PermissionHelper.requestPermissions(this, REQUEST_PERMISSIONS);
        } else {
            startBleScan();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
            @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_PERMISSIONS) {
            if (PermissionHelper.areAllPermissionsGranted(grantResults)) {
                gpsManager.startLocationUpdates(); // Start GPS updates when permissions granted
                startBleScan();
            } else {
                Toast.makeText(this, "Permissions required", Toast.LENGTH_SHORT).show();
            }
        }
    }

    private void startBleScan() {
        bleManager.startScan();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (gpsHandler != null && gpsUpdateRunnable != null) {
            gpsHandler.removeCallbacks(gpsUpdateRunnable);
        }
        if (gpsManager != null) {
            gpsManager.stopLocationUpdates();
        }
        if (bleManager != null) {
            bleManager.disconnect();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (gpsHandler != null && gpsUpdateRunnable != null) {
            gpsHandler.removeCallbacks(gpsUpdateRunnable);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (gpsHandler != null && gpsUpdateRunnable != null) {
            gpsHandler.postDelayed(gpsUpdateRunnable, GPS_UPDATE_INTERVAL_MS);
        }
    }

    private void dismissKeyboard() {
        InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null && getCurrentFocus() != null) {
            imm.hideSoftInputFromWindow(getCurrentFocus().getWindowToken(), 0);
        }
        if (binding.messageEditText != null) {
            binding.messageEditText.clearFocus();
        }
    }

    private void updateCharCount(String text) {
        if (text == null)
            text = "";
        int charCount = text.length();
        int packedBytes = Protocol.calculatePackedSize(text);
        int totalMessageSize = 12 + packedBytes; // 12 byte header + packed text

        String countText = charCount + "/" + Protocol.MAX_TEXT_LENGTH + " chars (" + totalMessageSize + " bytes)";
        binding.charCountTextView.setText(countText);

        // Change color if approaching limit
        if (charCount >= Protocol.MAX_TEXT_LENGTH) {
            binding.charCountTextView.setTextColor(0xFFFF0000); // Red
        } else if (charCount >= Protocol.MAX_TEXT_LENGTH * CHAR_COUNT_WARNING_THRESHOLD) {
            binding.charCountTextView.setTextColor(0xFFFF6600); // Orange
        } else {
            binding.charCountTextView.setTextColor(0xFF666666); // Gray
        }
    }

    private void startGpsPeriodicUpdates() {
        gpsHandler = new android.os.Handler(android.os.Looper.getMainLooper());
        gpsUpdateRunnable = new Runnable() {
            @Override
            public void run() {
                messageViewModel.updateGps();
                gpsHandler.postDelayed(this, GPS_UPDATE_INTERVAL_MS);
            }
        };
        gpsHandler.postDelayed(gpsUpdateRunnable, GPS_UPDATE_INTERVAL_MS);
    }
}
