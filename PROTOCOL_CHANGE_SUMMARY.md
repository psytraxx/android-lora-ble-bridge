# Protocol Change: Separate Text and GPS Messages

## Summary
Changed from a single `DataMessage` (text + GPS) to separate message types:
- `TextMessage` (0x01): Text only
- `GpsMessage` (0x02): GPS coordinates only  
- `AckMessage` (0x03): Acknowledgment

## Benefits

### 1. **Smaller Message Sizes**
- **Text only**: 4 + packed_text bytes (no GPS overhead)
  - Example: "SOS" = 7 bytes (was 15 bytes with GPS)
  - Example: 50-char message = 42 bytes (was 50 bytes with GPS)
- **GPS only**: 10 bytes fixed (no text overhead)
  - Always 10 bytes regardless of text
- **Combined savings**: If sending text + GPS separately, still often smaller

### 2. **Flexible Update Rates**
- Send GPS updates more frequently (e.g., every 30s)
- Send text messages only when needed
- Don't waste bandwidth sending same GPS with every text

### 3. **Better for Different Use Cases**
- **Emergency SOS**: Just text, no GPS needed if location is separate
- **Position tracking**: GPS-only updates without text
- **Status updates**: Text without redundant GPS

## Wire Format Changes

### Old Format (DataMessage 0x01)
```
[Type:1][Seq:1][CharCount:1][PackedLen:1][PackedText:var][Lat:4][Lon:4]
Total: 12 + packed_text_bytes
Example "SOS": 15 bytes (12 + 3)
Example 50-char: 50 bytes (12 + 38)
```

### New Formats

**TextMessage (0x01)**
```
[Type:1][Seq:1][CharCount:1][PackedLen:1][PackedText:var]
Total: 4 + packed_text_bytes
Example "SOS": 7 bytes (4 + 3) - SAVES 8 bytes!
Example 50-char: 42 bytes (4 + 38) - SAVES 8 bytes!
```

**GpsMessage (0x02)**
```
[Type:1][Seq:1][Lat:4][Lon:4]
Total: 10 bytes (fixed)
ALWAYS 10 bytes regardless of context
```

**AckMessage (0x03)** - unchanged
```
[Type:1][Seq:1]
Total: 2 bytes
```

## Size Comparisons

| Scenario | Old (Text+GPS) | New (Separate) | Savings |
|----------|----------------|----------------|---------|
| "SOS" only | 15 bytes | 7 bytes | 8 bytes (53%) |
| "SOS" + GPS | 15 bytes | 17 bytes (7+10) | -2 bytes |
| 50-char only | 50 bytes | 42 bytes | 8 bytes (16%) |
| 50-char + GPS | 50 bytes | 52 bytes (42+10) | -2 bytes |
| GPS only | Not possible | 10 bytes | N/A |
| 10 text msgs + 1 GPS | 150 bytes | 80 bytes | 70 bytes (47%) |

**Key Insight**: If you don't send GPS with every text message, you save significantly!

## Transmission Time Impact (SF10, BW125, 433MHz)

| Message | Bytes | ToA (ms) |
|---------|-------|----------|
| Text "SOS" | 7 | ~200ms |
| Text 50-char | 42 | ~500ms |
| GPS only | 10 | ~250ms |
| Old 50-char+GPS | 50 | ~600ms |

## Implementation Status

- [x] ESP32 protocol.rs updated (TextMessage, GpsMessage, AckMessage with 6-bit packing)
- [x] ESP32 lora.rs updated (handles all 3 message types, sends ACKs)
- [x] ESP32 compiles successfully
- [x] ESP32 channel capacity increased (BLE→LoRa: 1→5 messages)
- [x] Android Protocol.java updated (3 message types with 6-bit packing)
- [x] Android MainActivity.java updated (sends text always, GPS only if available)
- [x] Android compiles successfully
- [x] Android tests created (9 comprehensive unit tests, all passing)
- [x] protocol.md documentation updated
- [x] README.md updated

## GPS Sending Behavior

**Current Implementation (MainActivity.java)**:
- Text message is **always sent** when user presses Send
- GPS message is **only sent if GPS is enabled and location is available**
- 100ms delay between text and GPS messages
- Toast notification shows:
  - "Sent text only (X bytes) - No GPS" if no GPS
  - "Sent text (X bytes) + GPS (Y bytes)" if GPS available

**Benefits**:
- Users can send messages even when GPS is disabled/unavailable
- Saves bandwidth when GPS not needed
- Still sends GPS automatically when available
- No UI changes needed (automatic behavior)

## Next Steps

✅ **All core implementation completed!**

**Optional Future Enhancements**:
1. Add UI toggle for "Send GPS with every message" vs "GPS on demand"
2. Add separate "Send GPS Update" button for location-only updates
3. Add visual indicator showing when GPS is being sent
4. Implement retry mechanism for failed messages
5. Add message history/log viewer
6. Add statistics: messages sent, bytes transmitted, duty cycle usage

## Backward Compatibility

⚠️ **BREAKING CHANGE** - Not backward compatible with old protocol!
- Old devices will not understand new message types
- Need to update all devices simultaneously
- Message type 0x01 has different meaning (was Data, now Text)
