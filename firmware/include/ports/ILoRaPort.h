#ifndef ILORA_PORT_H
#define ILORA_PORT_H

#include <cstdint>
#include <cstddef>

// Forward declarations - full definitions come from platform-specific LoRaManager.h
struct LoRaConfig;
struct LoRaPacket;

/**
 * @brief LoRa Port Interface (Hexagonal Architecture)
 *
 * Abstracts LoRa radio functionality across different platforms:
 * - ESP32: RadioLib with SX1262/SX1278
 * - nRF52: RadioLib with SX1262
 */
class ILoRaPort
{
public:
    virtual ~ILoRaPort() = default;

    /**
     * @brief Initialize LoRa radio with configuration
     * @param config LoRa parameters (frequency, SF, BW, CR, power)
     * @return true on success, false on failure
     */
    virtual bool begin(const LoRaConfig &config) = 0;

    /**
     * @brief Start receive mode (listening for packets)
     * @param dutyCycle Enable duty cycle mode for power saving
     * @return true on success, false on failure
     */
    virtual bool startReceive(bool dutyCycle) = 0;

    /**
     * @brief Start transmitting data
     * @param data Pointer to data buffer
     * @param len Length of data to transmit
     * @return true if transmission started, false on failure
     */
    virtual bool startTransmit(const uint8_t *data, size_t len) = 0;

    /**
     * @brief Process pending LoRa events (RX/TX completion)
     * Must be called regularly in main loop
     */
    virtual void process() = 0;

    /**
     * @brief Set callback for received packets
     * @param callback Function to call when packet received
     */
    virtual void setReceiveCallback(void (*callback)(const LoRaPacket &packet)) = 0;

    /**
     * @brief Set callback for transmission completion
     * @param callback Function to call when transmission completes (success/failure)
     */
    virtual void setTransmitCallback(void (*callback)(bool success)) = 0;
};

#endif // ILORA_PORT_H
