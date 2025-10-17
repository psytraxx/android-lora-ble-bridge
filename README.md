# LoRa Android RS

A long-range communication system for sending text messages (up to 50 characters) and GPS coordinates via 433 MHz LoRa using ESP32-S3 and Android devices.

## Features

- 📱 **Android App**: Java-based app with GPS integration and BLE communication
- 📡 **Long Range**: 5-10 km typical range (up to 15+ km in ideal conditions)
- 🔋 **Efficient**: Optimized 433 MHz LoRa (SF10, BW125) for range vs speed
- ✅ **Reliable**: ACK mechanism confirms message delivery
- 🌍 **GPS Precision**: ±1 meter accuracy (coordinates × 1,000,000)
- 🚀 **Fast**: ~1-2 second end-to-end latency

## Architecture

See **[architecture.md](architecture.md)** for the detailed system diagram and requirements.

## Project Structure

```
lora-android-rs/
├── esp32s3/              # ESP32-S3 firmware (Rust)
│   ├── src/
│   │   ├── main.rs       # Main entry point
│   │   ├── ble.rs        # BLE GATT server and event handling
│   │   ├── lora.rs       # LoRa radio driver and message handling
│   │   └── protocol.rs   # Binary protocol implementation
│   └── Cargo.toml
├── android/              # Android application (Java)
│   ├── app/src/main/
│   │   ├── java/
│   │   │   ├── com/lora/android/MainActivity.java
│   │   │   └── lora/Protocol.java
│   │   └── res/layout/activity_main.xml
│   └── build.gradle
├── protocol.md           # Protocol specification
├── architecture.md       # System architecture
└── README.md            # This file
```

## Protocol

See **[protocol.md](protocol.md)** for the complete binary message format specification.

## Building & Installation

### Prerequisites

#### ESP32 Firmware
- [Rust](https://rustup.rs/) (stable)
- [espup](https://github.com/esp-rs/espup) - ESP32 Rust toolchain installer
- ESP32-S3 development board
- SX1276 LoRa module

#### Android App
- [Android Studio](https://developer.android.com/studio) or Android SDK
- JDK 8 or higher
- Gradle (included in Android Studio)

### ESP32 Firmware Build

1. **Install ESP32 Rust toolchain:**
   ```bash
   cargo install espup
   espup install
   . ~/export-esp.sh  # Source the environment
   ```

2. **Build firmware:**
   ```bash
   cd esp32s3
   cargo build --release
   ```

3. **Flash to ESP32-S3:**
   ```bash
   cargo run --release
   # Or use espflash:
   espflash flash target/xtensa-esp32s3-none-elf/release/esp32s3 --monitor
   ```

4. **Monitor logs:**
   ```bash
   espflash monitor
   ```

### Android App Build

#### Using Android Studio
1. Open the `android/` folder in Android Studio
2. Wait for Gradle sync to complete
3. Connect Android device or start emulator
4. Click "Run" or press Shift+F10

#### Using Command Line
```bash
cd android
./gradlew assembleDebug           # Build APK
./gradlew installDebug             # Install to connected device
```

### Running Tests

**ESP32 Firmware:**
```bash
cd esp32s3
cargo check                        # Type checking
cargo clippy                       # Linting
```

**Android App:**
```bash
cd android
./gradlew test                     # Run unit tests (37 tests)
./gradlew connectedAndroidTest     # Run instrumentation tests
```

## Hardware Setup

### ESP32-S3 to SX1276 Wiring

| SX1276 Pin | ESP32-S3 Pin | Function |
|------------|--------------|----------|
| SCK | GPIO18 | SPI Clock |
| MISO | GPIO19 | SPI MISO |
| MOSI | GPIO21 | SPI MOSI |
| NSS/CS | GPIO5 | Chip Select |
| RESET | GPIO12 | Reset |
| DIO0 | GPIO15 | Interrupt |
| 3.3V | 3.3V | Power |
| GND | GND | Ground |

### LoRa Module Configuration

For detailed configuration including frequency, power, and regional compliance, see **[LORA_CONFIG.md](LORA_CONFIG.md)**.

**Quick Start:**
```toml
# Edit esp32s3/.cargo/config.toml
[env]
LORA_TX_POWER_DBM = "14"        # Power in dBm (-4 to 20)
LORA_TX_FREQUENCY = "433920000" # Frequency in Hz (433/868/915 MHz bands)
```

**Antenna:** Use antenna tuned for your chosen frequency (~17 cm for 433 MHz quarter-wave)

## Usage

### Android App

1. **Launch app** on both Android devices
2. **Grant permissions**: Bluetooth, Location (GPS)
3. **Wait for BLE connection**: App automatically scans for "ESP32S3-LoRa"
4. **Send message**:
   - Type message (max 50 characters)
   - Ensure GPS has fix (shown in app)
   - Press "Send"
5. **Receive message**: Messages appear automatically on receiving device
6. **View GPS location**: Coordinates displayed with received messages

### First-Time Setup

1. Flash firmware to both ESP32-S3 devices
2. Install app on both Android phones
3. Power on both ESP32 devices (observe BLE advertising in logs)
4. Open app on Phone A → connects to ESP32-A
5. Open app on Phone B → connects to ESP32-B
6. Test with short message like "Test" from Phone A
7. Verify receipt on Phone B
8. Check ACK notification on Phone A

## Performance & Specifications

See **[protocol.md](protocol.md)** for detailed performance characteristics including Time on Air calculations and duty cycle compliance.

**Quick Reference:**
- Max text: 50 characters (61 bytes total)
- Typical range: 5-10 km (up to 15+ km ideal conditions)
- Latency: 1-2 seconds end-to-end
- See **[LORA_CONFIG.md](LORA_CONFIG.md)** for range estimates by frequency and power

## Troubleshooting

### ESP32 Issues

**BLE not advertising:**
- Check serial monitor for "BLE advertising..." message
- Verify Bluetooth is enabled in ESP32 logs
- Restart ESP32 (power cycle)

**LoRa not transmitting:**
- Check SPI wiring (SCK, MISO, MOSI, CS)
- Verify 3.3V power to LoRa module
- Check antenna connection (433 MHz antenna)
- Monitor serial for "LoRa TX successful" messages

**Radio init failed:**
- Check RESET and DIO0 pin connections
- Verify SX1276 module is 433 MHz capable
- Check power supply (some modules need more current)

### Android Issues

**App can't find ESP32:**
- Grant Bluetooth and Location permissions
- Enable Bluetooth on phone
- Ensure ESP32 is powered and advertising
- Check that device name is "ESP32S3-LoRa" in logs
- Try restarting both phone and ESP32

**No GPS fix:**
- Go outdoors or near window
- Wait 30-60 seconds for GPS acquisition
- Check Location permission is granted
- Enable "High accuracy" in phone location settings

**Messages not received:**
- Check both ESP32 devices are powered
- Verify LoRa range (start close, then test distance)
- Check serial monitor for "LoRa RX: received X bytes"
- Ensure devices are on same frequency (433 MHz)

### Debug Tips

**ESP32 Serial Monitor:**
```bash
espflash monitor
# Look for:
# - "BLE advertising..."
# - "LoRa radio ready for RX/TX"
# - "Message forwarded from BLE to LoRa"
# - "LoRa TX successful"
# - "LoRa RX: received X bytes"
```

**Android Logcat:**
```bash
adb logcat -s LoRaApp
# Or use Android Studio's Logcat viewer
```

## Message Flow

See **[protocol.md](protocol.md#message-flow)** and **[architecture.md](architecture.md)** for detailed message flow diagrams.

## Documentation

### Project Documentation
- **`protocol.md`**: Complete binary protocol specification with examples
- **`architecture.md`**: System architecture and component interaction
- **`OPTIMIZATION_SUMMARY.md`**: LoRa configuration and performance tuning
- **`PROTOCOL_COMPATIBILITY.md`**: Java/Rust protocol compatibility analysis
- **`TEST_REPORT.md`**: Android unit test results and coverage

### External Resources
- [ESP32-S3 Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
- [SX1276 Datasheet](https://www.semtech.com/products/wireless-rf/lora-core/sx1276)
- [LoRa Calculator](https://www.loratools.nl/#/airtime) - Time on Air calculator
- [ESP-RS Book](https://esp-rs.github.io/book/) - Rust on ESP32

## License

[Add your license here]

## Contributing

[Add contribution guidelines here]

## Acknowledgments

Built with:
- [Embassy](https://embassy.dev/) - Async Rust framework for embedded
- [esp-hal](https://github.com/esp-rs/esp-hal) - ESP32 Hardware Abstraction Layer
- [trouble-host](https://github.com/embassy-rs/trouble) - BLE Host stack
- [lora-phy](https://github.com/lora-rs/lora-rs) - LoRa PHY driver

---

**Ready for long-range adventures!** 📡🌍