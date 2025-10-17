# LoRa Android RS

A long-range communication system for sending text messages (up to 50 characters) and GPS coordinates via 433 MHz LoRa using ESP32-S3 and Android devices.

## Features

- 📱 **Android App**: Java-based app with GPS integration and BLE communication
- 📡 **Long Range**: 5-10 km typical range (up to 15+ km in ideal conditions)
- 🔋 **Power Optimized**: 40-50% power savings (70-100 hours on 2500 mAh battery)
- 📦 **Message Buffering**: Buffers up to 10 messages when phone is disconnected
- ✅ **Reliable**: ACK mechanism confirms message delivery
- 🌍 **GPS Precision**: ±1 meter accuracy
- 🚀 **Fast**: ~1-2 second end-to-end latency

## Recent Improvements

### Power Optimization (40-50% savings)
- **CPU Clock**: Reduced from 240 MHz to 160 MHz
- **Auto Light Sleep**: Enabled via Embassy async framework
- **Battery Life**: 70-100 hours on 2500 mAh (was 50-60 hours)

### Message Buffering
- **Buffer Capacity**: 10 messages (was 1)
- **Behavior**: Continues receiving LoRa messages even when phone is disconnected
- **On Reconnect**: All buffered messages delivered immediately
- **Memory Cost**: Only 640 bytes of RAM

## Architecture

```mermaid
graph TD
    A[Android Phone 1<br/>- Internal GPS<br/>- Text Input<br/>- Display<br/>- Java App] -->|Text + GPS Data| B[BLE]
    B --> C[ESP32-S3<br/>LoRa Transmitter<br/>- Sx1276 Module<br/>- Pins: SCK18, MISO19, MOSI21, SS5, RST12, DIO015<br/>- Firmware: Rust/Arduino]
    C -->|LoRa Transmission| D[LoRa Radio Waves]
    D --> E[ESP32-S3<br/>LoRa Receiver<br/>- Same hardware/firmware]
    E -->|Forwarded Data| F[BLE]
    F --> G[Android Phone 2<br/>- Display<br/>- Receives Text + GPS<br/>- Same Java App]
    
    E -->|ACK| D
    D --> C
    C -->|ACK| B
    B --> A
    
    subgraph "Sender Side"
        A
        B
        C
    end
    
    subgraph "Receiver Side"
        E
        F
        G
    end
```

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

Edit `esp32s3/.cargo/config.toml`:

```toml
[env]
LORA_TX_POWER_DBM = "14"        # Power in dBm (-4 to 20)
LORA_TX_FREQUENCY = "433920000" # Frequency in Hz
```

**Common Frequencies:**
- 433 MHz: 433920000 (worldwide)
- 868 MHz: 868100000 (Europe)
- 915 MHz: 915000000 (Americas, Australia)

**Regional Power Limits:**
- EU (433 MHz): 2 dBm max
- US (433 MHz): 17 dBm max
- US (915 MHz): 30 dBm max
- Australia: 14 dBm (433 MHz) / 30 dBm (915 MHz)

**Antenna:** Use antenna tuned for your chosen frequency (~17 cm for 433 MHz quarter-wave)

## Message Buffering

The ESP32 firmware buffers up to 10 messages when your phone is disconnected:

**When Phone is Connected:**
- Messages delivered instantly

**When Phone is Disconnected:**
- Messages buffered (up to 10)
- ESP32 continues receiving
- Sender gets ACK immediately

**When You Reconnect:**
- All buffered messages delivered instantly
- Oldest messages first (FIFO)

**If Buffer is Full:**
- Messages 11+ are dropped with warning log
- ESP32 continues receiving (doesn't block)

**Adjusting Buffer Size:**

Edit `esp32s3/src/bin/main.rs`:
```rust
// Change 10 to desired size (5-50 recommended)
static LORA_TO_BLE: StaticCell<Channel<CriticalSectionRawMutex, Message, 10>> = StaticCell::new();
```

Also update function signatures in `esp32s3/src/ble.rs` and `esp32s3/src/lora.rs`.

**Memory Impact:** ~64 bytes per message

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

## Performance

- **Max text**: 50 characters (61 bytes total)
- **Range**: 5-10 km typical (up to 15+ km ideal conditions)
- **Latency**: 1-2 seconds end-to-end
- **Battery**: 70-100 hours on 2500 mAh (2500 mAh battery)
- **Time on Air**: ~370ms (empty) to ~700ms (50 chars) at SF10
- **LoRa Config**: SF10, BW125kHz, CR4/5, 433.92 MHz default, 14 dBm

See **[protocol.md](protocol.md)** for detailed Time on Air calculations and duty cycle compliance.

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

## External Resources
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