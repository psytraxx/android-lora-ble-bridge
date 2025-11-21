# nRF52 LoRa-BLE Bridge Firmware

This is the nRF52 port of the ESP32 LoRa-BLE Bridge firmware, designed for the **Seeed XIAO nRF52840** microcontroller connected to an **SX1262 LoRa radio module**.

## Hardware Requirements

- **Microcontroller**: Seeed XIAO nRF52840
- **LoRa Module**: SX1262 (433 MHz or appropriate frequency for your region)
- **Connections**:
  - SX1262 SCK → XIAO P0.26 (D8)
  - SX1262 MISO → XIAO P0.29 (D9)
  - SX1262 MOSI → XIAO P0.02 (D10)
  - SX1262 CS → XIAO P0.03 (D0)
  - SX1262 RST → XIAO P0.28 (D1)
  - SX1262 DIO0 → XIAO P0.04 (D2)
  - SX1262 BUSY → XIAO P0.05 (D3)

## Features

- ✅ **BLE Peripheral** using ArduinoBLE library
- ✅ **LoRa Communication** using RadioLib with SX1262
- ✅ **Protocol Compatibility** with ESP32 firmware
- ✅ **FreeRTOS Tasks** for concurrent BLE and LoRa operations
- ✅ **Battery Monitoring** (ADC on P0.31)
- ✅ **Power Management** (duty-cycled LoRa RX)
- ⚠️ **Low-Power Modes** (not yet implemented)
- ⚠️ **Message Buffering** (simplified version)

## Protocol Compatibility

This firmware uses the same message protocol as the ESP32 version, defined in `../firmware/src/Protocol.cpp`. It supports:
- **Text Messages**: Up to 50 characters with 6-bit encoding
- **GPS Messages**: Text + optional GPS coordinates
- **ACK Messages**: Acknowledgment with sequence numbers
- **WakeUp Messages**: Wake devices from low-power mode

## Build and Flash

### Prerequisites

1. Install [PlatformIO](https://platformio.org/install)
2. Connect Seeed XIAO nRF52840 via USB

### Build

```bash
cd firmware-nrf52
pio run -e xiao_nrf52840
```

### Upload

```bash
pio run -e xiao_nrf52840 --target upload
```

### Monitor Serial Output

```bash
pio device monitor --baud 115200
```

Or combined upload + monitor:

```bash
pio run -e xiao_nrf52840 --target upload --target monitor
```

## Configuration

### Device Name

To change the BLE device name, edit `platformio.ini`:

```ini
-DDEVICE_NAME='"nRF52-LoRa (1)"'
```

### LoRa Parameters

LoRa parameters are defined in `include/FirmwareConfig.h` and must match the ESP32 firmware:

```cpp
LORA_FREQUENCY = 433.92  // MHz
LORA_SPREADING_FACTOR = 9
LORA_BANDWIDTH = 250     // kHz
LORA_CODING_RATE = 5     // 4/5
LORA_TX_POWER = 20       // dBm
```

### Pin Mappings

If you're using different GPIO pins, update them in `include/FirmwareConfig.h`:

```cpp
namespace PinConfig
{
    constexpr int LORA_SCK = 26;   // P0.26 (Arduino D8)
    constexpr int LORA_MISO = 29;  // P0.29 (Arduino D9)
    // ... etc
}
```

## Architecture

### FreeRTOS Tasks

The firmware uses three FreeRTOS tasks:

1. **LoRa Task** (Priority 4 - Highest)
   - Processes LoRa RX/TX events
   - Forwards BLE messages to LoRa
   - Handles interrupt-driven transmission

2. **BLE Task** (Priority 3)
   - Polls BLE events
   - Forwards LoRa messages to BLE
   - Manages BLE connection state

3. **Power Task** (Priority 2 - Lowest)
   - Monitors battery level
   - Handles inactivity timeouts
   - Will manage sleep modes (future)

### Message Flow

```
Android App <--BLE--> BLE Task <--Queue--> LoRa Task <--LoRa--> Other Devices
                         ↓                       ↓
                   ApplicationController   PowerManager
```

## Troubleshooting

### Build Errors

1. **"RadioLib.h not found"**
   - Run: `pio lib install`

2. **"ArduinoBLE.h not found"**
   - Check platformio.ini has: `arduino-libraries/ArduinoBLE@^1.3.6`

3. **Undefined reference to FreeRTOS functions**
   - Ensure build flag: `-DUSE_FREERTOS`

### Runtime Issues

1. **LoRa initialization fails**
   - Check SPI wiring (SCK, MISO, MOSI, CS)
   - Verify RST and BUSY pins
   - Check power supply (SX1262 needs 3.3V)

2. **BLE not visible**
   - Check Serial output for "BLE initialized successfully"
   - Verify device name is set correctly
   - Scan with nRF Connect app

3. **Messages not forwarding**
   - Check queue sizes in `FirmwareConfig.h`
   - Monitor Serial for "queue full" messages
   - Verify protocol serialization

## Testing

### Test BLE Connection

1. Build and flash firmware
2. Open Serial monitor (115200 baud)
3. Scan for "nRF52-LoRa (1)" with nRF Connect app
4. Connect and discover services
5. Check Serial for "BLE connected"

### Test LoRa Reception

1. Use another LoRa device (ESP32 firmware) to send messages
2. Monitor Serial output for "LoRa packet received"
3. Check RSSI and SNR values
4. Verify message is deserialized correctly

### Test LoRa Transmission

1. Connect via BLE
2. Write a message to RX characteristic
3. Monitor Serial for "Starting transmission"
4. Check other LoRa device receives the message
5. Verify LED blinks (2 blinks = TX, 1 blink = RX)

## Differences from ESP32 Firmware

| Feature | ESP32 | nRF52 |
|---------|-------|-------|
| **BLE Stack** | NimBLE | ArduinoBLE |
| **Framework** | ESP-IDF | Arduino |
| **Storage** | NVS | Not yet implemented (FDS planned) |
| **Power Modes** | Deep Sleep | Not yet implemented (System OFF planned) |
| **LoRa Radio** | SX1262 or SX1278 | SX1262 only |
| **Memory** | 520KB RAM | 256KB RAM |

## Future Improvements

- [ ] Implement Flash Data Storage (FDS) for message buffering
- [ ] Add System OFF low-power mode with wake sources
- [ ] Optimize power consumption (target <2mA average)
- [ ] Add OTA firmware updates via BLE
- [ ] Support SX1278 radio (if needed)
- [ ] Add comprehensive error handling
- [ ] Implement watchdog timer

## License

Same as parent project.

## Support

For issues specific to nRF52 firmware, please include:
- Serial monitor output
- Hardware configuration
- Build logs from PlatformIO
