# Meshtastenstein: Custom Firmware with Meshtastic Protocol

## Context

The current firmware has a killer feature — SX1262 autonomous duty cycling with 64-symbol preamble — achieving ~52 days battery life on 2500mAh. Meshtastic's client mode drains battery because it uses continuous RX or software-based duty cycling + mesh flooding overhead.

**Goal**: Build a custom firmware that speaks the Meshtastic protocol (BLE API + OTA packet format) so that official Meshtastic apps can connect, while preserving the power-efficient hardware duty cycle. This replaces the custom 6-bit binary protocol and custom BLE service.

**Great news**: The current LoRa settings (SF11, BW250, CR4/5, 433.92MHz) exactly match Meshtastic's `LongFast` preset — no radio changes needed!

**Trade-off**: The 64-symbol preamble means standard Meshtastic nodes (16-symbol preamble) may not wake this device from duty cycle. But this device's transmissions (64-sym) will be received by all standard Meshtastic nodes. This is acceptable — the power savings are worth it, and it's configurable.

---

## Architecture Overview

```
┌─────────────────────────────────────┐
│  Meshtastic App (Android/iOS/Web)   │
│  (unmodified, official apps)        │
└──────────────┬──────────────────────┘
               │ BLE (Meshtastic UUIDs)
               │ FromRadio/ToRadio protobuf
┌──────────────▼──────────────────────┐
│  Custom Firmware ("Meshtastenstein") │
│  ┌─────────────────────────────┐    │
│  │ MeshtasticBLE               │    │  ← NEW: Meshtastic BLE service
│  │ (FromRadio/ToRadio/FromNum) │    │
│  └─────────────┬───────────────┘    │
│  ┌─────────────▼───────────────┐    │
│  │ MeshProtocol                │    │  ← NEW: protobuf encode/decode
│  │ (nanopb, MeshPacket, Data)  │    │
│  └─────────────┬───────────────┘    │
│  ┌─────────────▼───────────────┐    │
│  │ MeshCrypto                  │    │  ← NEW: AES256-CTR encryption
│  │ (channel PSK, nonce)        │    │
│  └─────────────┬───────────────┘    │
│  ┌─────────────▼───────────────┐    │
│  │ LoRaManager (REUSE)         │    │  ← Keep: RadioLib, duty cycle
│  │ + sync word 0x2B            │    │
│  └─────────────┬───────────────┘    │
│  ┌─────────────▼───────────────┐    │
│  │ NodeDB + Config (NEW)       │    │  ← NEW: node info storage
│  └─────────────────────────────┘    │
│  Platform Traits (REUSE)            │  ← Keep: ESP32/nRF52 abstraction
│  PowerManager (REUSE)               │  ← Keep: duty cycle, deep sleep
└─────────────────────────────────────┘
```

---

## Phase 1: Protobuf Foundation + Basic OTA Compatibility ✅ COMPLETE

**Goal**: Send/receive Meshtastic-compatible LoRa packets (text messages) between two devices running this firmware. No BLE yet — serial debug only.

### 1.1 Add nanopb and Meshtastic protobufs ✅ DONE
### 1.2 Implement MeshPacket OTA format ✅ DONE
### 1.3 Implement AES256-CTR encryption ✅ DONE
### 1.4 Node identity ✅ DONE
### 1.5 Test with two devices — READY FOR TESTING

**What was implemented:**
- Added nanopb 0.4.9 library to platformio.ini
- Vendored all Meshtastic `.proto` files from `meshtastic/protobufs` repo
- Generated `.pb.h`/`.pb.cpp` files (23 files) for all protocols
- Created `MeshCrypto` module (AES256-CTR with mbedtls)
- Created `MeshPacket` module (OTA packet format: 16-byte header + encrypted payload)
- Created `NodeDB` module (node identity from MAC, packet ID generation)
- Created `MeshProtocol` bridge layer (converts between custom Message and Meshtastic)
- Updated LoRa sync word to 0x2B (Meshtastic) when `MESHTASTIC_PROTOCOL` is defined
- Integrated into `unified_main.cpp` with #ifdef guards
- Build succeeds: 671KB flash (20.1%), 37KB RAM (11.4%)

**Files created:**
- `firmware/include/meshtastic/` — 23 protobuf headers
- `firmware/src/meshtastic/` — 23 protobuf sources
- `firmware/include/common/MeshCrypto.h`
- `firmware/src/common/MeshCrypto.cpp`
- `firmware/include/common/MeshPacket.h`
- `firmware/src/common/MeshPacket.cpp`
- `firmware/include/common/NodeDB.h`
- `firmware/src/common/NodeDB.cpp`
- `firmware/include/common/MeshProtocol.h`
- `firmware/src/common/MeshProtocol.cpp`

**Configuration:**
- Build flag `-DMESHTASTIC_PROTOCOL` enables Meshtastic protocol
- Default channel key: 0x01 (Meshtastic default "AQ==")
- Sync word: 0x2B (Meshtastic network)
- LoRa settings unchanged: SF11, BW250, CR4/5 (matches Meshtastic `LongFast` preset!)
- Duty cycle preserved: 64-symbol preamble

**Platform Support:**
- ✅ **ESP32 (Heltec WiFi LoRa V3)**: 671KB flash (20.1%), 37KB RAM (11.4%) — Uses mbedtls hardware AES
- ✅ **nRF52 (Seeed XIAO nRF52840)**: 213KB flash (26.3%), 14KB RAM (6.0%) — Uses rweather/Crypto library (same as Meshtastic firmware)
  - `meshtastic/mesh.proto` (MeshPacket, Data, Position, User, NodeInfo, FromRadio, ToRadio)
  - `meshtastic/portnums.proto` (PortNum enum)
  - `meshtastic/config.proto` (Config — needed for BLE config flow)
  - `meshtastic/module_config.proto` (ModuleConfig)
  - `meshtastic/channel.proto` (Channel, ChannelSettings)
  - `meshtastic/telemetry.proto` (DeviceMetrics for battery)
  - `meshtastic/deviceonly.proto` (NodeInfoLite, DeviceState)
- Generate nanopb `.h`/`.c` files from `.proto` files (nanopb generator or pre-generate)
- Create `firmware/include/common/MeshProtocol.h` — wrapper for protobuf encode/decode

**Files to create**:
- `firmware/proto/` — directory for .proto files and nanopb options
- `firmware/include/common/MeshProtocol.h` — protobuf serialization wrapper
- `firmware/src/common/MeshProtocol.cpp`

### 1.2 Implement MeshPacket OTA format
- **Packet header** (unencrypted, 16 bytes):
  - `from`: 4 bytes (fixed32, LE) — sender node number
  - `to`: 4 bytes (fixed32, LE) — destination (0xFFFFFFFF = broadcast)
  - `id`: 4 bytes (fixed32, LE) — unique packet ID
  - `flags`: 1 byte — channel_index(4 bits) | hop_limit(3 bits) | want_ack(1 bit)
  - `channel_hash`: 1 byte — derived from channel name + PSK
  - `reserved`: 2 bytes (padding/future use)
- **Payload**: AES256-CTR encrypted protobuf `Data` message
- Change LoRa sync word from default to `0x2B` (Meshtastic network identifier)

**Files to modify**:
- `firmware/include/common/LoRaManager.h` — add sync word config
- `firmware/src/common/LoRaManager.cpp` — set sync word 0x2B

**Files to create**:
- `firmware/include/common/MeshCrypto.h` — AES256-CTR encrypt/decrypt
- `firmware/src/common/MeshCrypto.cpp`
- `firmware/include/common/MeshPacket.h` — OTA packet serialization

### 1.3 Implement AES256-CTR encryption
- Use ESP32's hardware AES (mbedtls) or a lightweight AES library for nRF52
- Default channel key: expand 1-byte key `0x01` to 256-bit via key expansion (Meshtastic uses `AQ==` base64 = `0x01`, expanded by repeating/hashing)
- Nonce construction: packet_id (4 bytes) + from_node (4 bytes) + zero padding to 16 bytes
- Encrypt/decrypt the `Data` protobuf payload only (header stays clear)

### 1.4 Node identity
- Generate node number from MAC address (lower 4 bytes, like Meshtastic does)
- Store short_name (4 chars) and long_name in NVS/flash
- Create `firmware/include/common/NodeDB.h` — minimal node database (own node + recently seen nodes)

### 1.5 Test with two devices
- Both devices transmit text messages as Meshtastic MeshPackets
- Verify OTA compatibility by sniffing with a real Meshtastic device (if available)
- Keep existing custom protocol as compile-time option (`#ifdef MESHTASTIC_PROTOCOL`)

---

## Phase 2: Meshtastic BLE Service

**Goal**: Official Meshtastic Android app can connect, see the device, and send/receive text messages.

### 2.1 Replace BLE service with Meshtastic UUIDs
- **Service UUID**: `6ba1b218-15a8-461f-9fa8-5dcae273eafd`
- **Characteristics**:
  - `FromRadio` (`2c55e69e-4993-11ed-b878-0242ac120002`): READ — device sends protobuf to client
  - `ToRadio` (`f75c76d2-129e-4dad-a1dd-7866124401e7`): WRITE — client sends protobuf to device
  - `FromNum` (`ed9da18c-a800-4f66-a670-aa7547e34453`): READ+NOTIFY+WRITE — packet counter

### 2.2 Implement config download flow
When a Meshtastic client connects and sends `want_config_id`, the device must stream:
1. `FromRadio { my_info: MyNodeInfo { my_node_num, ... } }`
2. `FromRadio { metadata: DeviceMetadata { firmware_version, hw_model, ... } }`
3. `FromRadio { config: Config { device, position, power, network, display, lora, bluetooth } }`
4. `FromRadio { moduleConfig: ModuleConfig { ... } }` (for each module)
5. `FromRadio { channel: Channel { index, settings { ... } } }` (for each channel)
6. `FromRadio { node_info: NodeInfo { num, user, position } }` (for each known node)
7. `FromRadio { config_complete_id: <matching_id> }`

### 2.3 Handle ToRadio messages
- `ToRadio { packet: MeshPacket }` → encode and transmit via LoRa
- `ToRadio { want_config_id }` → trigger config download
- `ToRadio { disconnect }` → clean disconnect
- `ToRadio { heartbeat }` → keep-alive

### 2.4 Forward received LoRa packets to BLE
- When a MeshPacket is received via LoRa, decrypt it, wrap in `FromRadio { packet }`, queue for BLE
- Increment FromNum counter, notify client

### 2.5 Advertise as Meshtastic device
- BLE advertising must include the Meshtastic service UUID
- Device name format: `Meshtastic_XXXX` (last 4 hex of node number)

---

## Phase 3: Essential Meshtastic Features

**Goal**: Full text messaging + position + node discovery working with official apps.

### 3.1 NodeInfo broadcasting
- Periodically broadcast own `NodeInfo` (User + Position) via `NODEINFO_APP` (port 4)
- Store received NodeInfo from other nodes in NodeDB
- Forward to BLE clients

### 3.2 Position handling
- Receive `POSITION_APP` (port 3) messages from other nodes
- If device has GPS (or client sends position), broadcast position
- Encode/decode `Position` protobuf

### 3.3 Routing / ACK
- Implement basic `ROUTING_APP` (port 5) for delivery acknowledgment
- When receiving a packet with `want_ack=true`, send ACK response
- Track sent packet IDs for ACK matching

### 3.4 Telemetry
- Send `TELEMETRY_APP` (port 67) with `DeviceMetrics` (battery level, voltage)
- Reuse existing battery monitoring from PowerManager

### 3.5 Duplicate packet detection
- Maintain a rolling cache of recently seen packet IDs (by `from` + `id`)
- Drop duplicates (critical for mesh flooding)

---

## Phase 4: Mesh Compatibility & Admin

**Goal**: Interoperate with standard Meshtastic mesh network.

### 4.1 Packet rebroadcasting (optional/configurable)
- Rebroadcast received packets if hop_limit > 0 (decrement hop_limit)
- Make this configurable — can disable for power savings (CLIENT_MUTE role)
- SNR-based rebroadcast priority timing

### 4.2 Admin module (port 6)
- Handle basic `ADMIN_APP` messages for config read/write from clients
- Allow Meshtastic app to change channel settings, LoRa config, device name
- Store config changes in NVS

### 4.3 Channel management
- Support multiple channels (Meshtastic supports 8)
- Each channel has name + PSK → different encryption keys
- Channel hash computation for OTA channel identification

### 4.4 PKC / Direct Messages (v2.5+)
- Generate X25519 key pair on first boot
- Implement ECDH key exchange for DM encryption (AES-CCM)
- Broadcast public key in NodeInfo

---

## What Gets REUSED from Current Codebase

| Component | Status | Notes |
|-----------|--------|-------|
| `LoRaManager` | **REUSE** (modify) | Change sync word to 0x2B, keep duty cycle |
| `PlatformTraits` | **REUSE** as-is | ESP32/nRF52 abstraction unchanged |
| `PowerManager` | **REUSE** as-is | Duty cycle, deep sleep, battery monitoring |
| `LEDManager` | **REUSE** as-is | Status LED patterns |
| `FirmwareConfig.h` | **REUSE** (modify) | Update BLE UUIDs, add Meshtastic constants |
| `MessageQueue` | **REUSE** (modify) | Change message type from custom to MeshPacket |
| `unified_main.cpp` | **REUSE** (modify) | Same loop structure, different message handling |
| `Protocol.h/cpp` | **REPLACE** | Custom 6-bit protocol → Meshtastic protobuf |
| `BLEManager` (both) | **REWRITE** | New UUIDs, new protobuf-based protocol |

## What Gets ADDED
- `nanopb` library for protobuf on embedded
- Meshtastic `.proto` definitions (vendored subset)
- `MeshProtocol` — protobuf encode/decode wrapper
- `MeshCrypto` — AES256-CTR encryption
- `MeshPacket` — OTA packet format (header + encrypted payload)
- `NodeDB` — node database (own + neighbor nodes)
- `ConfigManager` — device config in NVS
- New `BLEManager` implementations with Meshtastic BLE API

---

## Milestones

| # | Name | What works | Scope |
|---|------|-----------|-------|
| 1 | "It speaks Meshtastic" | Two devices exchange text via Meshtastic OTA format | ~2000 LOC |
| 2 | "The app connects" | Official Meshtastic app connects via BLE, send/receive text | ~1500 LOC |
| 3 | "Full citizen" | Position, NodeInfo, telemetry, ACKs | ~1000 LOC |
| 4 | "Mesh player" | Packet relay, admin, multi-channel | ~1500 LOC |

---

## Key Technical Decisions

1. **Start with Phase 1 on ESP32 only** — nRF52 follows once protocol is stable
2. **Use nanopb** — the standard C protobuf library for embedded, used by Meshtastic firmware itself
3. **Vendor a minimal set of .proto files** — don't need the full Meshtastic protobuf repo
4. **Keep duty cycle as-is** — 64-symbol preamble, document the trade-off vs standard 16-symbol
5. **hw_model = PRIVATE_HW** — use Meshtastic's `PRIVATE_HW` enum value for custom hardware
6. **Default to CLIENT_MUTE role** — no mesh relaying by default, preserves battery
7. **Keep custom protocol behind #ifdef** — allows A/B testing during development

---

## Meshtastic Protocol Reference

### Key Protobuf Messages
```protobuf
MeshPacket { from, to, channel, decoded(Data)|encrypted(bytes), id, rx_time, rx_snr, hop_limit, want_ack, priority, rx_rssi, hop_start }
Data { portnum, payload, want_response, dest, source, request_id, reply_id, emoji }
Position { latitude_i(sfixed32), longitude_i(sfixed32), altitude, time, timestamp }
User { id, long_name, short_name, hw_model, is_licensed, role, public_key }
NodeInfo { num, user, position, snr, last_heard, device_metrics, channel }
FromRadio { id, oneof: packet/my_info/node_info/config/config_complete_id/rebooted/moduleConfig/channel/queueStatus/metadata }
ToRadio { oneof: packet/want_config_id/disconnect/heartbeat }
```

### Key PortNums
```
TEXT_MESSAGE_APP = 1
POSITION_APP = 3
NODEINFO_APP = 4
ROUTING_APP = 5
ADMIN_APP = 6
TEXT_MESSAGE_COMPRESSED_APP = 7
TELEMETRY_APP = 67
```

### BLE UUIDs
```
Service:   6ba1b218-15a8-461f-9fa8-5dcae273eafd
FromRadio: 2c55e69e-4993-11ed-b878-0242ac120002  (read)
ToRadio:   f75c76d2-129e-4dad-a1dd-7866124401e7  (write)
FromNum:   ed9da18c-a800-4f66-a670-aa7547e34453  (read, notify, write)
```

### OTA Encryption
- AES256-CTR, channel PSK as key
- Default key: `AQ==` (0x01, expanded to 256-bit)
- Nonce: packet_id(4) + from_node(4) + zero_pad(8)
- Sync word: 0x2B
