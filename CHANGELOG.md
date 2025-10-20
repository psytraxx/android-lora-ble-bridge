## Recent Improvements

### Protocol v2.0 - Separate Message Types (Oct 2025)
- **Separate Messages**: Text and GPS now sent as independent messages
  - `TextMessage` (0x01): Text only with 6-bit packing
  - `GpsMessage` (0x02): GPS coordinates only (10 bytes fixed)
  - `AckMessage` (0x03): Acknowledgments
- **Bandwidth Savings**: 
  - Text-only: 40% smaller (7 bytes for "SOS" vs 15 bytes)
  - 6-bit encoding: 25% smaller than UTF-8 for uppercase text
  - GPS optional: Only sent when GPS is enabled
- **Flexible Usage**:
  - Send text without GPS when location not needed
  - Send GPS updates separately for tracking
  - Text always sent, GPS only when available

### Power Optimization (40-50% savings)
- **CPU Clock**: Reduced from 240 MHz to 160 MHz
- **Auto Light Sleep**: Enabled via Embassy async framework
- **Battery Life**: 70-100 hours on 2500 mAh (was 50-60 hours)

### Message Buffering
- **Buffer Capacity**: 10 messages (was 1)
- **BLE→LoRa Channel**: 5 messages (increased from 1 for text+GPS bursts)
- **Behavior**: Continues receiving LoRa messages even when phone is disconnected
- **On Reconnect**: All buffered messages delivered immediately