//! nRF52 Platform Entry Point (Hexagonal Architecture)
//!
//! This file provides the platform-specific implementation for nRF52.
//! It creates concrete adapters and injects them into the unified main application.

#include "ports/PlatformPorts.h"
#include "nrf52/FirmwareConfig.h"
#include "nrf52/adapters/NRF52BLEAdapter.h"
#include "nrf52/adapters/NRF52LoRaAdapter.h"
#include "nrf52/adapters/NRF52StorageAdapter.h"
#include "nrf52/adapters/NRF52PowerAdapter.h"
#include "nrf52/adapters/NRF52SystemAdapter.h"
#include "nrf52/adapters/NRF52ActivityAdapter.h"
#include "common/MessageQueue.h"

// External message queue needed by nRF52 BLEManager
extern MessageQueue bleToLoraQueue;

// Platform-specific configuration functions for main.cpp
LoRaConfig getPlatformLoRaConfig()
{
    return {
        .frequency = LORA_FREQUENCY,
        .bandwidth = LORA_BANDWIDTH,
        .spreadingFactor = LORA_SPREADING_FACTOR,
        .codingRate = LORA_CODING_RATE,
        .txPower = LORA_TX_POWER};
}

unsigned long getPowerInactivityTimeout()
{
    return PowerConstants::INACTIVITY_TIMEOUT_MS;
}

int getLedRxBlinks()
{
    return LEDConstants::RX_BLINKS;
}

int getLedTxBlinks()
{
    return LEDConstants::TX_BLINKS;
}

/**
 * @brief Create nRF52-specific platform adapters
 *
 * This factory function instantiates all nRF52 adapters:
 * - NRF52BLEAdapter (Bluefruit)
 * - NRF52LoRaAdapter (RadioLib with SX1262)
 * - NRF52StorageAdapter (LittleFS)
 * - NRF52PowerAdapter (Internal ADC-based battery monitoring)
 * - NRF52SystemAdapter (No watchdog, direct GPIO LED)
 * - NRF52ActivityAdapter (ApplicationController, simple state)
 *
 * Note: Logging is done directly via Serial (no adapter needed)
 */
PlatformPorts createPlatformPorts()
{
    PlatformPorts ports;

    ports.system = new NRF52SystemAdapter();
    ports.power = new NRF52PowerAdapter();
    ports.storage = new NRF52StorageAdapter();
    ports.activity = new NRF52ActivityAdapter();

    // nRF52 BLEManager needs a reference to the BLE-to-LoRa queue
    ports.ble = new NRF52BLEAdapter(&bleToLoraQueue);

    ports.lora = new NRF52LoRaAdapter(
        LORA_SCK,
        LORA_MISO,
        LORA_MOSI,
        LORA_SS,
        LORA_RST,
        LORA_DIO0,
        LORA_BUSY);

    return ports;
}

/**
 * @brief Get nRF52 device name from configuration
 */
const char *getDeviceName()
{
    return DEVICE_NAME;
}
