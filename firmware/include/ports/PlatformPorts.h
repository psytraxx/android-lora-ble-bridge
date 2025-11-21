#ifndef PLATFORM_PORTS_H
#define PLATFORM_PORTS_H

#include <cstdint>

// LoRa types need to be defined before the port interfaces
// These are the same across platforms (defined in LoRaManager.h)
#ifndef LORA_CONFIG_DEFINED
#define LORA_CONFIG_DEFINED
struct LoRaConfig
{
    float frequency;         // MHz (e.g., 433.92)
    float bandwidth;         // kHz (e.g., 31.25)
    uint8_t spreadingFactor; // SF (7-12)
    uint8_t codingRate;      // CR (5-8 for 4/5 to 4/8)
    int8_t txPower;          // dBm (e.g., 20)
};

struct LoRaPacket
{
    uint8_t buffer[256]; // 256 bytes = max LoRa payload (RadioLib limit)
    int len;             // Actual packet length
    int rssi;            // Received Signal Strength Indicator (dBm)
    float snr;           // Signal-to-Noise Ratio (dB)
};
#endif

#include "ports/IBLEPort.h"
#include "ports/ILoRaPort.h"
#include "ports/IStoragePort.h"
#include "ports/IPowerPort.h"
#include "ports/ISystemPort.h"
#include "ports/IActivityPort.h"

/**
 * @brief Platform Ports Structure (Hexagonal Architecture)
 *
 * Aggregates all port interfaces for dependency injection
 * Each platform implements createPlatformPorts() to provide concrete adapters
 *
 * Note: Logging is done directly via Serial (Arduino standard) - no port needed
 */
struct PlatformPorts
{
    IBLEPort *ble;
    ILoRaPort *lora;
    IStoragePort *storage;
    IPowerPort *power;
    ISystemPort *system;
    IActivityPort *activity;
};

/**
 * @brief Platform-specific factory function
 *
 * Each platform (ESP32, nRF52) implements this function to create
 * and return platform-specific adapters
 */
extern PlatformPorts createPlatformPorts();

/**
 * @brief Get device name (platform-specific)
 */
extern const char *getDeviceName();

#endif // PLATFORM_PORTS_H
