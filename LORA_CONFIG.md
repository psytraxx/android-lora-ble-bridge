# LoRa Radio Configuration

## Overview
The ESP32-S3 firmware supports configurable LoRa transmission power and frequency via environment variables. This allows compliance with regional regulations and flexible deployment across different ISM bands.

## Configuration

### Setting TX Power and Frequency
Edit `esp32s3/.cargo/config.toml` and set the environment variables:

```toml
[env]
# Transmission power in dBm
LORA_TX_POWER_DBM = "14"

# Frequency in Hz
LORA_TX_FREQUENCY = "433920000"
```

## Transmission Power Configuration

### Regional Limits (by ISM Band)

#### 433 MHz ISM Band (433.05 - 434.79 MHz)

| Region | Maximum TX Power | Notes |
|--------|-----------------|-------|
| **European Union** | 2 dBm (1.6 mW) ERP | EN 300 220, very restrictive |
| **United States** | 17 dBm (~50 mW) | FCC Part 15.247, duty cycle limited |
| **Japan** | 2 dBm (1.6 mW) | ARIB STD-T108, similar to EU |
| **Australia** | 14 dBm (~25 mW) | ACMA regulations |
| **China** | 10 dBm (10 mW) | SRRC regulations |

#### 868 MHz Band (863 - 870 MHz)

| Region | Maximum TX Power | Notes |
|--------|-----------------|-------|
| **European Union** | 14-27 dBm | Depends on duty cycle and channel, ETSI EN 300 220 |

#### 915 MHz Band (902 - 928 MHz)

| Region | Maximum TX Power | Notes |
|--------|-----------------|-------|
| **United States** | 30 dBm (1 W) | FCC Part 15.247 |
| **Australia** | 30 dBm (1 W) | ACMA regulations |

**Important:** Always verify current regulations in your country/region before deployment.

### Hardware Support
The SX1276 LoRa module supports TX power from **-4 dBm to +20 dBm** on the PA_BOOST pin. The firmware validates that the configured power is within this range:

- If the value is out of range, it defaults to 14 dBm
- If the variable is not set, it defaults to 14 dBm
- If the value cannot be parsed, it defaults to 14 dBm

## Frequency Configuration

### Common ISM Bands

| Band | Frequency Range | Common Frequencies | Regions |
|------|----------------|-------------------|---------|
| **433 MHz** | 433.05 - 434.79 MHz | 433.92 MHz, 434.665 MHz | Worldwide |
| **868 MHz** | 863 - 870 MHz | 868.1 MHz, 868.3 MHz, 868.5 MHz | Europe, India, Russia |
| **915 MHz** | 902 - 928 MHz | 915.0 MHz, 923.3 MHz | Americas, Australia, New Zealand |

### Popular Frequency Choices

#### 433 MHz Band
- **433.920 MHz** (433920000 Hz) - Default, most commonly used
- **434.665 MHz** (434665000 Hz) - Alternative channel, less crowded

#### 868 MHz Band (Europe)
- **868.100 MHz** (868100000 Hz) - LoRaWAN channel 0
- **868.300 MHz** (868300000 Hz) - LoRaWAN channel 1
- **868.500 MHz** (868500000 Hz) - LoRaWAN channel 2

#### 915 MHz Band (Americas, Australia)
- **915.000 MHz** (915000000 Hz) - Common US frequency
- **923.300 MHz** (923300000 Hz) - Australia

### Frequency Validation
The firmware validates frequencies against common ISM bands:
- 433.05 - 434.79 MHz (433,050,000 - 434,790,000 Hz)
- 863 - 870 MHz (863,000,000 - 870,000,000 Hz)
- 902 - 928 MHz (902,000,000 - 928,000,000 Hz)

If an invalid frequency is provided, it defaults to 433.92 MHz.

### Important Notes
1. **Antenna Matching**: Your antenna must be tuned for the chosen frequency
2. **Network Consistency**: All devices in your network must use the same frequency
3. **Regional Compliance**: Verify the frequency is legal in your region
4. **Interference**: Different frequencies may have different interference levels

## Implementation Details

### Compile-Time Configuration
Both TX power and frequency are read from environment variables at compile time using Rust's `option_env!()` macro. This means:
1. No runtime overhead
2. Settings are fixed at compile time
3. Must rebuild firmware to change settings

### Code Location
Configuration is implemented in `esp32s3/src/lora.rs`:
- **TX Power**: Lines 106-131 (parse and validate)
- **Frequency**: Lines 133-162 (parse and validate)
- **Application**: Lines 191 & 267 (TX power), Line 173 (frequency)

### Validation
The firmware performs the following checks:

**TX Power:**
1. Parse string to i32
2. Validate range: -4 dBm ≤ power ≤ 20 dBm
3. Log the configured power or fallback to default
4. Apply power to both data messages and ACK transmissions

**Frequency:**
1. Parse string to u32
2. Validate against ISM bands (433, 868, or 915 MHz)
3. Log the configured frequency or fallback to default
4. Apply frequency to modulation parameters

## Configuration Examples

### EU Deployment (433 MHz)
```toml
[env]
LORA_TX_POWER_DBM = "2"        # EU legal limit
LORA_TX_FREQUENCY = "433920000" # 433.92 MHz
```

### EU Deployment (868 MHz)
```toml
[env]
LORA_TX_POWER_DBM = "14"       # Up to 27 dBm with duty cycle control
LORA_TX_FREQUENCY = "868100000" # 868.1 MHz (LoRaWAN CH0)
```

### US Deployment (433 MHz)
```toml
[env]
LORA_TX_POWER_DBM = "17"        # Maximum for 433 MHz
LORA_TX_FREQUENCY = "433920000" # 433.92 MHz
```

### US Deployment (915 MHz)
```toml
[env]
LORA_TX_POWER_DBM = "20"        # Can use higher power in 915 MHz
LORA_TX_FREQUENCY = "915000000" # 915.0 MHz
```

### Australia Deployment
```toml
[env]
LORA_TX_POWER_DBM = "14"        # Safe for 433 MHz
LORA_TX_FREQUENCY = "433920000" # 433.92 MHz
# Or for 915 MHz:
# LORA_TX_POWER_DBM = "20"
# LORA_TX_FREQUENCY = "923300000"
```

### Japan Deployment
```toml
[env]
LORA_TX_POWER_DBM = "2"         # Legal limit
LORA_TX_FREQUENCY = "433920000" # 433.92 MHz
```

### China Deployment
```toml
[env]
LORA_TX_POWER_DBM = "10"        # Legal limit
LORA_TX_FREQUENCY = "433920000" # 433.92 MHz
```

## Testing

### Verify Configuration
To verify your settings:

1. Set desired power and frequency in `config.toml`
2. Rebuild the firmware: `cd esp32s3 && cargo build`
3. Flash to ESP32: `espflash flash --monitor target/xtensa-esp32s3-none-elf/debug/main`
4. Check the serial output for:
   - "Using TX power from config: X dBm"
   - "Using frequency from config: X Hz (X.XX MHz)"

### Build and Flash Commands
```bash
# Check compilation
cd esp32s3
cargo check

# Build firmware
cargo build --release

# Flash and monitor
cargo run --release
```

## Range Estimates

### By Frequency and Power

| Frequency | TX Power | Typical Range | Notes |
|-----------|----------|--------------|-------|
| 433 MHz | 2 dBm | 1-2 km | EU/Japan legal limit |
| 433 MHz | 10 dBm | 3-5 km | China legal limit |
| 433 MHz | 14 dBm | 5-10 km | Default, Australia limit |
| 433 MHz | 17 dBm | 8-12 km | US legal limit |
| 433 MHz | 20 dBm | 10-15 km | Maximum hardware capability |
| 868 MHz | 14 dBm | 4-8 km | EU with duty cycle |
| 915 MHz | 20 dBm | 8-15 km | US/Australia |

*Range estimates assume line-of-sight, SF10, BW125kHz, and minimal interference. Lower frequencies (433 MHz) generally provide slightly better range than higher frequencies (915 MHz) due to better diffraction and less free-space path loss.*

## Safety and Compliance Notes

1. **Legal Compliance**: Using excessive TX power or wrong frequencies can violate local regulations and result in fines
2. **Duty Cycle**: Some regions (e.g., EU) also limit transmission duty cycle (typically 1% or 10%)
3. **Antenna**: Ensure your antenna is designed for your chosen frequency to avoid impedance mismatch
4. **Power Consumption**: Higher TX power increases battery drain significantly
5. **Interference**: Always verify your chosen frequency doesn't interfere with critical services
6. **Testing**: Test thoroughly in your deployment environment before production use

## Troubleshooting

### Common Issues

**"Invalid frequency value" warning**
- Check that LORA_TX_FREQUENCY is a valid number without quotes or units
- Ensure frequency is in Hz (e.g., 433920000, not 433.92)

**"Frequency outside common ISM bands" warning**
- Verify the frequency is within: 433.05-434.79 MHz, 863-870 MHz, or 902-928 MHz
- Check for typos (e.g., extra or missing zeros)

**Poor range despite high power**
- Verify antenna is tuned for your frequency
- Check for obstacles between devices
- Verify both devices use the same frequency
- Consider switching to lower frequency (433 MHz vs 915 MHz)

**Device not communicating**
- Ensure all devices use the **exact same frequency**
- Verify spreading factor and bandwidth match
- Check TX power isn't too low

## Future Enhancements
Possible improvements:
1. **Runtime Configuration**: Allow changing TX power and frequency via BLE commands
2. **Automatic Channel Scanning**: Detect and switch to clearest channel
3. **Adaptive Power Control**: Adjust power based on RSSI feedback
4. **Duty Cycle Tracking**: Monitor transmission duty cycle for EU compliance
5. **Multi-Channel Support**: Use channel hopping for better reliability
6. **Frequency Hopping**: Implement FHSS for interference resistance

## See Also
- **[README.md](README.md)** - Project overview and quick start
- **[architecture.md](architecture.md)** - System architecture
- **[protocol.md](protocol.md)** - Message format specification
