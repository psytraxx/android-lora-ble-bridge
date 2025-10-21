# ESP32 Quick Start Guide

## Hardware Setup

1. **Connect LoRa Module to ESP32:**
   ```
   LoRa SX1276/SX1278 → ESP32
   ─────────────────────────────
   VCC    → 3.3V
   GND    → GND
   SCK    → GPIO 18
   MISO   → GPIO 19
   MOSI   → GPIO 23
   NSS/CS → GPIO 5
   RST    → GPIO 12
   DIO0   → GPIO 32
   ```

2. **Attach Antenna:** Connect a 433 MHz antenna to the LoRa module

3. **Power ESP32:** Connect via USB or external power supply

## Software Setup

### 1. Install PlatformIO

**Option A: VSCode Extension**
- Install "PlatformIO IDE" extension in VSCode
- Restart VSCode

**Option B: Command Line**
```bash
pip install platformio
```

### 2. Open Project
```bash
cd esp32/
```

### 3. Build and Upload
```bash
# Build
pio run

# Upload
pio run --target upload

# Monitor
pio device monitor -b 115200

# All in one
pio run --target upload && pio device monitor
```

## Testing

### 1. Verify LoRa Initialization
You should see:
```
LoRa Configuration:
  Frequency: 433.92 MHz
  Bandwidth: 125.0 kHz
  Spreading Factor: 10
  Coding Rate: 4/5
  TX Power: 14 dBm
LoRa initialized successfully.
```

### 2. Verify BLE Advertising
You should see:
```
BLE service created
Device name: ESP32-LoRa
Starting BLE advertising...
```

### 3. Connect Android App
1. Open the Android app
2. Scan for "ESP32-LoRa"
3. Connect
4. You should see: `BLE client connected`

### 4. Send Test Message
1. Type "SOS" in the Android app
2. Press Send
3. You should see:
   ```
   Received message from BLE: type=1
   Transmitting 7 bytes via LoRa
   LoRa TX successful
   ```

## Configuration

### Change TX Power (2-20 dBm)
Edit `platformio.ini`:
```ini
build_flags = 
    -DLORA_TX_POWER_DBM=20  ; Change to desired power
```

### Change Frequency
Edit `platformio.ini`:
```ini
build_flags = 
    -DLORA_TX_FREQUENCY=868000000  ; Example: 868 MHz
```

### Change Pin Mapping
Edit `src/main.cpp`:
```cpp
#define LORA_SCK 18    // Change pins as needed
#define LORA_MISO 19
#define LORA_MOSI 23
// ... etc
```

## Troubleshooting

### "LoRa initialization failed!"
- ✓ Check wiring (especially SPI pins)
- ✓ Verify 3.3V power supply
- ✓ Ensure antenna is connected
- ✓ Try resetting the ESP32

### "BLE not visible"
- ✓ Restart ESP32
- ✓ Check Bluetooth is enabled on phone
- ✓ Move closer to ESP32
- ✓ Check serial output for errors

### "Message not received on other device"
- ✓ Both devices on same frequency
- ✓ Check antenna connections
- ✓ Verify line of sight or reduce distance
- ✓ Check RSSI value (should be > -120 dBm)

### Monitor RSSI
Messages will show:
```
LoRa RX: received 7 bytes, RSSI: -85 dBm, SNR: 8.5 dB
```

Good signal: RSSI > -100 dBm
Weak signal: RSSI -100 to -120 dBm
Too weak: RSSI < -120 dBm

## Serial Monitor Commands

Monitor output at 115200 baud:
```bash
pio device monitor -b 115200
```

## Testing Two Devices

### Device A (ESP32 #1):
1. Upload firmware
2. Connect Android App A
3. Send message "HELLO FROM A"

### Device B (ESP32 #2):
1. Upload firmware
2. Connect Android App B
3. Should receive "HELLO FROM A"
4. Send reply "HELLO FROM B"

### Expected Serial Output:

**Device A:**
```
Received message from BLE: type=1
Transmitting 16 bytes via LoRa
LoRa TX successful
...
LoRa RX: received 2 bytes, RSSI: -87 dBm
Received ACK for seq: 1
```

**Device B:**
```
LoRa RX: received 16 bytes, RSSI: -85 dBm
Text message - seq: 1, text: "HELLO FROM A"
Sending ACK for seq: 1
ACK sent successfully
Text message forwarded from LoRa to BLE
```

## Performance Tips

### Maximize Range
- Use 20 dBm TX power (check regulations)
- Elevate antennas
- Ensure line of sight
- Use SF10 or SF12 (already default)

### Maximize Battery Life
- Reduce TX power to 10-14 dBm
- Implement sleep mode (not included)
- Reduce BLE advertising interval

### Maximize Data Rate
- Use SF7 instead of SF10 (edit lora_config.h)
- Increase bandwidth to 250 kHz
- Note: This reduces range significantly

## Default Settings

```
Device Name:    ESP32-LoRa
BLE Service:    00001234-0000-1000-8000-00805f9b34fb
TX Char:        00005678-0000-1000-8000-00805f9b34fb
RX Char:        00005679-0000-1000-8000-00805f9b34fb

Frequency:      433.92 MHz
TX Power:       14 dBm (~25 mW)
Spreading:      SF10
Bandwidth:      125 kHz
Coding Rate:    4/5

Max Text:       50 characters
Max Range:      5-10 km typical
Time on Air:    ~550 ms (max message)
```

## Support

For issues or questions:
1. Check serial monitor output
2. Verify hardware connections
3. Review protocol.md for message format
4. Check IMPLEMENTATION.md for details

## License

Same as parent project.
