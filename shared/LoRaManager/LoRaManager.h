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
    LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, long frequency) noexcept;
    ~LoRaManager();

    // Non-copyable
    LoRaManager(const LoRaManager &) = delete;
    LoRaManager &operator=(const LoRaManager &) = delete;

    // Lifecycle
    bool setup();
    void shutdown() noexcept;

    // Non-blocking transmit helpers (RadioLib pattern)
    int startTransmitNonBlocking(const uint8_t *buffer, size_t length);
    void finishTransmit() noexcept;

    // Receive mode management
    void startReceiveMode() noexcept;

    // TX bookkeeping accessors
    bool consumeTxDoneFlag() noexcept;

    // RX helpers
    bool consumeRxFlag() noexcept;
    bool isRxPending() const noexcept;
    LoRaPacket getPacketData() noexcept;

    // Debug/config helpers
    String getConfigurationString() const noexcept;

    // Polling helper to be called regularly from main loop. This will
    // recover stuck transmissions (e.g. if IRQ was lost) and perform
    // non-ISR cleanup when necessary.
    void poll() noexcept;

private:
    // Pins and configuration
    const int sckPin;
    const int misoPin;
    const int mosiPin;
    const int ssPin;
    const int rstPin;
    const int dio0Pin;
    const long frequency;

    // Owned RadioLib objects (opaque in header)
    Module *module;
    SX1278 *radio;

    // Transmission bookkeeping. txDoneFlag is set from the ISR when TX is done.
    volatile bool txDoneFlag = false;

    // Track if we're currently sending (set in startTransmitNonBlocking,
    // cleared in ISR/onTxDone). Allows callers to avoid starting another TX
    // and enables poll() to detect stuck transmissions.
    volatile bool sendingFlag = false;

    // Millis when last TX was started (used for timeout/recovery)
    unsigned long lastTxStart = 0;
    // Receive flag set from ISR
    volatile bool rxFlag = false;

    // Static instance pointer for ISR routing
    static LoRaManager *s_instance;

    // Static callbacks used by RadioLib ISR
    static void onTxDoneStatic() noexcept;
    static void onRxStatic() noexcept;

    // Optional: set custom packet-sent action
    void setPacketSentAction(void (*callback)(void)) noexcept;

    void onReceive(void (*callback)(void)) noexcept;
};
