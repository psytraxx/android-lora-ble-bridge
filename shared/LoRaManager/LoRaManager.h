#include <Arduino.h>
#include "lora_config.h"

// Forward-declare RadioLib types to avoid including heavy headers in the public header
class Module;
class SX1278;

// Lightweight struct for LoRa packets with metadata
struct LoRaPacket
{
    uint8_t buffer[256];
    int len = 0;
    int rssi = 0;
    float snr = 0.0f;
};

class LoRaManager
{
public:
    LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, float frequency) noexcept;
    ~LoRaManager();

    // Non-copyable
    LoRaManager(const LoRaManager &) = delete;
    LoRaManager &operator=(const LoRaManager &) = delete;

    // Lifecycle
    bool setup();
    void shutdown() noexcept;

    // Blocking transmit (simple and reliable like old version)
    bool sendPacketBlocking(const uint8_t *buffer, size_t length);

    // Receive mode management
    void startReceiveMode() noexcept;

    // RX helpers
    bool consumeRxFlag() noexcept;
    bool isRxPending() const noexcept;
    LoRaPacket getPacketData() noexcept;

    // Debug/config helpers
    String getConfigurationString() const noexcept;

private:
    // Pins and configuration
    const int sckPin;
    const int misoPin;
    const int mosiPin;
    const int ssPin;
    const int rstPin;
    const int dio0Pin;
    const float frequency;

    // Owned RadioLib objects (opaque in header)
    Module *module;
    SX1278 *radio;

    // Receive flag set from ISR
    volatile bool rxFlag = false;

    // Static instance pointer for ISR routing
    static LoRaManager *s_instance;

    // Static callback used by RadioLib ISR for RX
    static void onRxStatic() noexcept;
};
