#include <common/LoRaManager.h>
#include "common/Logging.h"
#include "common/FirmwareConfig.h"
#include <Arduino.h>
#include <SPI.h>
#include <radio/sx126x/sx126x.h> // For SX126xSetDioIrqParams and IRQ defines

static const char *TAG = "LoRa";

// Platform-specific includes will be handled via FirmwareConfig later
// For now, we'll use conditional compilation for constants

// SX126x-Arduino library globals (must be static, not extern)
static hw_config hwConfig;
static RadioEvents_t RadioEvents;

// Pending RX packet storage (filled by callback, processed by process())
static volatile bool rxPending = false;
static uint8_t rxBuffer[256];
static uint16_t rxSize = 0;
static int16_t rxRssi = 0;
static int8_t rxSnr = 0;

// Pending TX complete flag
static volatile bool txPending = false;

// nRF52 requires a separate SPI instance for LoRa
#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52_SERIES)
SPIClass SPI_LORA(NRF_SPIM2, LORA_MISO, LORA_SCK, LORA_MOSI);
#endif

// Static instance for ISR access
LoRaManager *LoRaManager::instance = nullptr;

LoRaManager::LoRaManager(int sck, int miso, int mosi, int ss, int rst, int dio0, int busy)
    : state(STATE_UNINITIALIZED),
      lastRSSI(0),
      lastSNR(0.0f),
      receiveCallback(nullptr),
      transmitCallback(nullptr),
      pinSCK(sck),
      pinMISO(miso),
      pinMOSI(mosi),
      pinSS(ss),
      pinRST(rst),
      pinDIO0(dio0),
      pinBusy(busy)
{
    // Set singleton instance for ISR access
    instance = this;
}

// Private helper to set up hwConfig struct
void LoRaManager::setupHwConfig()
{
#if defined(RADIO_SX1262)
    hwConfig.CHIP_TYPE = SX1262_CHIP;
#elif defined(RADIO_SX1268)
    hwConfig.CHIP_TYPE = SX1268_CHIP;
#else
#error "No supported RADIO defined! Please define RADIO_SX1262, or RADIO_SX1268"
#endif
    hwConfig.PIN_LORA_RESET = pinRST;
    hwConfig.PIN_LORA_NSS = pinSS;
    hwConfig.PIN_LORA_SCLK = pinSCK;
    hwConfig.PIN_LORA_MISO = pinMISO;
    hwConfig.PIN_LORA_MOSI = pinMOSI;
    hwConfig.PIN_LORA_DIO_1 = pinDIO0; // DIO1 is used for interrupts
    hwConfig.PIN_LORA_BUSY = pinBusy;
    hwConfig.RADIO_TXEN = -1; // Not used
    hwConfig.RADIO_RXEN = -1; // Not used
    hwConfig.USE_DIO2_ANT_SWITCH = true;
    hwConfig.USE_DIO3_TCXO = true;
    hwConfig.USE_DIO3_ANT_SWITCH = false;
    hwConfig.USE_LDO = false;          // Use DCDC
    hwConfig.USE_RXEN_ANT_PWR = false; // Not used
    hwConfig.TCXO_CTRL_VOLTAGE = TCXO_CTRL_3_3V;
}

// Private helper to set up RadioEvents callbacks
void LoRaManager::setupRadioCallbacks()
{
    RadioEvents.TxDone = OnTxDone;
    RadioEvents.RxDone = OnRxDone;
    RadioEvents.TxTimeout = nullptr;
    RadioEvents.RxTimeout = nullptr;
    RadioEvents.RxError = nullptr;
    RadioEvents.CadDone = nullptr;
}

// Private helper to configure radio parameters
void LoRaManager::configureRadioParams()
{
    Radio.SetChannel(static_cast<uint32_t>(LoRaConstants::FREQUENCY * 1000000));
    Radio.SetTxConfig(MODEM_LORA, LORA_TX_POWER, 0, LoRaConstants::BANDWIDTH,
                      LoRaConstants::SPREADING_FACTOR, LoRaConstants::CODING_RATE,
                      LoRaConstants::PREAMBLE_LENGTH, false, true, false, 0, false, 5000);
    Radio.SetRxConfig(MODEM_LORA, LoRaConstants::BANDWIDTH, LoRaConstants::SPREADING_FACTOR,
                      LoRaConstants::CODING_RATE, 0, LoRaConstants::PREAMBLE_LENGTH,
                      0, false, 0, true, false, 0, false, true);
    Radio.SetCustomSyncWord(LoRaConstants::SYNC_WORD);
}

// Private helper to log radio configuration
void LoRaManager::logRadioConfig()
{
    LOG_I(TAG, "LoRa setup successful");
    LOG_I(TAG, "  Frequency: %.2f MHz", LoRaConstants::FREQUENCY);
    static const char *bwNames[] = {"125", "250", "500"};
    LOG_I(TAG, "  Bandwidth: %s kHz", bwNames[LoRaConstants::BANDWIDTH]);
    LOG_I(TAG, "  Spreading Factor: %d", LoRaConstants::SPREADING_FACTOR);
    LOG_I(TAG, "  Coding Rate: 4/%d", LoRaConstants::CODING_RATE + 4);
    LOG_I(TAG, "  TX Power: %d dBm", LORA_TX_POWER);
    LOG_I(TAG, "  Preamble Length: %d symbols", LoRaConstants::PREAMBLE_LENGTH);
    LOG_I(TAG, "  Sync Word: 0x%04X", LoRaConstants::SYNC_WORD);
}

bool LoRaManager::begin()
{
    LOG_I(TAG, "Initializing LoRa radio (cold start)");

#if defined(ARDUINO_ARCH_ESP32)
    // ESP32: Initialize SPI with custom pins
    SPI.begin(pinSCK, pinMISO, pinMOSI, pinSS);
#endif

    // Set up hardware configuration
    setupHwConfig();

    // Full hardware initialization
    if (lora_hardware_init(hwConfig) != 0)
    {
        LOG_E(TAG, "Failed to initialize LoRa hardware");
        return false;
    }

    // Set up callbacks and initialize radio
    setupRadioCallbacks();
    Radio.Init(&RadioEvents);

    // Configure radio parameters
    configureRadioParams();

    this->state = STATE_IDLE;
    logRadioConfig();
    return true;
}

bool LoRaManager::beginFromDeepSleep()
{
    LOG_I(TAG, "Initializing LoRa radio (warm start from deep sleep)");

#if defined(ARDUINO_ARCH_ESP32)
    // ESP32: Initialize SPI with custom pins
    SPI.begin(pinSCK, pinMISO, pinMOSI, pinSS);
#endif

    // Set up hardware configuration
    setupHwConfig();

    // Warm start - re-initialize without full reset
    // This preserves the SX126x state and pending IRQ
    if (lora_hardware_re_init(hwConfig) != 0)
    {
        LOG_E(TAG, "Failed to re-initialize LoRa hardware");
        // Fall back to full initialization
        return begin();
    }

    // Set up callbacks and re-initialize radio
    setupRadioCallbacks();
    Radio.ReInit(&RadioEvents);

    // Process pending IRQ from wake-up packet
    // This will trigger OnRxDone callback if a packet woke us up
    LOG_I(TAG, "Processing pending IRQ from deep sleep wake-up");
    Radio.IrqProcessAfterDeepSleep();

    this->state = STATE_IDLE;
    logRadioConfig();
    return true;
}

bool LoRaManager::startReceive(bool dutyCycle)
{
    if (state == STATE_UNINITIALIZED)
    {
        LOG_I(TAG, "Cannot start receive - not initialized");
        return false;
    }

    if (dutyCycle)
    {
        // Duty cycle mode for deep sleep - configure radio to wake MCU on packet
        // Based on SX126x-Arduino DeepSleep example

        // Put radio in standby first
        Radio.Standby();

        // Configure IRQ routing: RX_DONE and TIMEOUT go to DIO1 (which triggers MCU wake)
        SX126xSetDioIrqParams(
            IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT,  // IRQ mask
            IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT,  // DIO1 mask (wake source)
            IRQ_RADIO_NONE,                    // DIO2 mask
            IRQ_RADIO_NONE);                   // DIO3 mask

        // SX126x SetRxDutyCycle uses 15.625µs steps
        // To convert ms to steps: ms * 1000 / 15.625 = ms * 64
        // RX window should be long enough to detect preamble
        // With SF11/BW250: symbol time = 2^11/250000 = 8.192ms
        // Need at least 4 symbols to detect preamble: ~33ms
        // Using 50ms RX window = 50 * 64 = 3200 steps
        // Sleep time: 500ms = 500 * 64 = 32000 steps
        uint32_t rxSteps = 50 * 64;     // 50ms RX window
        uint32_t sleepSteps = 500 * 64; // 500ms sleep
        LOG_I(TAG, "Starting duty cycle RX mode (rx=%lums, sleep=%lums)",
              rxSteps / 64, sleepSteps / 64);
        Radio.SetRxDutyCycle(rxSteps, sleepSteps);
    }
    else
    {
        // Standard continuous receive mode
        LOG_I(TAG, "Starting continuous receive mode");
        Radio.Rx(0); // 0 = continuous RX
    }

    state = STATE_IDLE;
    return true;
}

bool LoRaManager::startTransmit(const uint8_t *data, size_t len, bool useLongPreamble)
{
    if (state == STATE_UNINITIALIZED)
    {
        LOG_I(TAG, "Cannot transmit - not initialized");
        return false;
    }

    if (state == STATE_TRANSMITTING)
    {
        LOG_I(TAG, "Transmission already in progress");
        return false;
    }

    // Configure preamble length for this transmission
    if (useLongPreamble)
    {
        // Set long preamble to wake up sleeping receivers
        LOG_I(TAG, "Using long preamble (%d symbols) for deep sleep wake-up", LoRaConstants::LONG_PREAMBLE_LENGTH);
        Radio.SetTxConfig(MODEM_LORA, LORA_TX_POWER, 0, LoRaConstants::BANDWIDTH,
                          LoRaConstants::SPREADING_FACTOR, LoRaConstants::CODING_RATE,
                          LoRaConstants::LONG_PREAMBLE_LENGTH, false, true, false, 0, false, 5000);
    }
    else
    {
        // Use standard preamble for normal transmission
        LOG_I(TAG, "Using standard preamble (%d symbols)", LoRaConstants::PREAMBLE_LENGTH);
        Radio.SetTxConfig(MODEM_LORA, LORA_TX_POWER, 0, LoRaConstants::BANDWIDTH,
                          LoRaConstants::SPREADING_FACTOR, LoRaConstants::CODING_RATE,
                          LoRaConstants::PREAMBLE_LENGTH, false, true, false, 0, false, 5000);
    }

    // Send message
    LOG_I(TAG, "Starting transmission of %d bytes", len);
    state = STATE_TRANSMITTING;
    Radio.Send((uint8_t *)data, len);

    LOG_I(TAG, "Transmission started");
    return true;
}

void LoRaManager::setReceiveCallback(LoRaReceiveCallback callback)
{
    receiveCallback = callback;
}

void LoRaManager::setTransmitCallback(LoRaTransmitCallback callback)
{
    transmitCallback = callback;
}

void LoRaManager::process()
{
    // Process pending RX packet (set by OnRxDone callback)
    if (rxPending)
    {
        rxPending = false;

        // Store packet stats
        lastRSSI = rxRssi;
        lastSNR = rxSnr;

        LOG_I(TAG, "Packet received (%d bytes, RSSI: %d dBm, SNR: %d dB)",
              rxSize, rxRssi, rxSnr);

        if (receiveCallback)
        {
            LoRaPacket packet;
            packet.len = rxSize;
            memcpy(packet.buffer, rxBuffer, rxSize);
            packet.rssi = rxRssi;
            packet.snr = rxSnr;
            receiveCallback(packet);
        }

        // Restart RX mode (use continuous for reliability)
        if (state != STATE_TRANSMITTING)
        {
            startReceive(false); // false = continuous RX
            state = STATE_IDLE;
        }
    }

    // Process pending TX complete (set by OnTxDone callback)
    if (txPending)
    {
        txPending = false;

        LOG_I(TAG, "TX complete");

        if (transmitCallback)
        {
            transmitCallback(true);
        }

        // Allow radio hardware to settle before switching to RX mode
        delay(LoRaConstants::RX_SETTLE_TIME_MS);

        // Restart RX mode (use continuous for reliability)
        startReceive(false); // false = continuous RX
        state = STATE_IDLE;
    }
}

int LoRaManager::getRSSI() const
{
    if (state == STATE_UNINITIALIZED)
        return 0;
    return lastRSSI;
}

float LoRaManager::getSNR() const
{
    if (state == STATE_UNINITIALIZED)
        return 0.0f;
    return lastSNR;
}

// ISR handlers
void LORA_ISR_ATTR LoRaManager::onReceiveISR()
{
    if (instance)
    {
        instance->state = STATE_PACKET_RECEIVED;
    }
}

void LORA_ISR_ATTR LoRaManager::onTransmitISR()
{
    if (instance)
    {
        instance->state = STATE_PACKET_SENT;
    }
}

// Callback wrappers for the new library
// NOTE: These run from a background task on ESP32/nRF52, not from ISR
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    LOG_I("LoRa", "OnRxDone callback: %d bytes, RSSI=%d, SNR=%d", size, rssi, snr);

    // Store packet data for processing in main loop
    if (size <= sizeof(rxBuffer))
    {
        memcpy(rxBuffer, payload, size);
        rxSize = size;
        rxRssi = rssi;
        rxSnr = snr;
        rxPending = true;
    }
    else
    {
        LOG_E("LoRa", "Packet too large: %d > %d", size, sizeof(rxBuffer));
    }
}

void OnTxDone(void)
{
    LOG_I("LoRa", "OnTxDone callback");
    txPending = true;
}
