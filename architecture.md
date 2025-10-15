```mermaid
graph TD
    A[Android Phone 1<br/>- Internal GPS<br/>- Text Input<br/>- Display<br/>- Java App] -->|Text + GPS Data| B[BLE]
    B --> C[ESP32-S3<br/>LoRa Transmitter<br/>- Sx1276 Module<br/>- Pins: SCK18, MISO19, MOSI23, SS5, RST12, DIO032<br/>- Firmware: Rust/Arduino]
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

## Requirements

- **Communication Interface**: Bluetooth Low Energy (BLE) between Android phones and ESP32-S3 modules.
- **Hardware**: ESP32-S3 development board with Sx1276 LoRa module. Pin definitions as specified.
- **Firmware**: ESP32-S3 runs firmware in Rust or Arduino (to be determined for efficiency).
- **Android App**: Java-based Android application that handles both sending and receiving messages and GPS coordinates.
- **Data Format**: Efficient binary format with minimal bytes for long-distance LoRa transmission, no encryption.
- **Features**: 
  - Send short text messages and GPS location from Phone 1 to Phone 2 via LoRa.
  - Receive messages and GPS on Phone 2.
  - Acknowledgments (ACK) sent back from receiver to sender to confirm delivery.
- **GPS**: Use Android phones' internal GPS for location data.