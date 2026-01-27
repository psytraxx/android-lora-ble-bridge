# Antenna Comparison Test

Simple RSSI/SNR test for comparing 433 MHz LoRa antennas using SX1262.

## Usage

1. Flash one device as **TX** (transmitter):
   ```bash
   # Edit src/main.cpp: MODE_TX=true, MODE_RX=false
   ~/.platformio/penv/bin/pio run -t upload
   ```

2. Flash second device as **RX** (receiver):
   ```bash
   # Edit src/main.cpp: MODE_TX=false, MODE_RX=true
   ~/.platformio/penv/bin/pio run -t upload
   ```

3. Place TX at fixed location, connect RX to serial monitor:
   ```bash
   ~/.platformio/penv/bin/pio device monitor
   ```

4. Collect ~10-20 packets with each antenna, compare averages.

## Interpreting Results

| Metric | Meaning |
|--------|---------|
| RSSI | Signal strength (higher/less negative = better) |
| SNR | Signal-to-noise ratio (higher = better) |

- **3+ dB difference** = meaningful
- **10+ dB difference** = significant
- **20+ dB difference** = one antenna is clearly superior