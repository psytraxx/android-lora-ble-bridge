#ifndef IPOWER_PORT_H
#define IPOWER_PORT_H

#include <cstdint>

/**
 * @brief Power Management Port Interface (Hexagonal Architecture)
 *
 * Abstracts battery monitoring and power management:
 * - ESP32: ADC with optional control pin
 * - nRF52: ADC with internal reference voltage
 */
class IPowerPort
{
public:
    virtual ~IPowerPort() = default;

    /**
     * @brief Initialize power management hardware
     * @return true on success, false on failure
     */
    virtual bool begin() = 0;

    /**
     * @brief Read battery level
     * @return Battery level percentage (0-100)
     */
    virtual uint8_t readBatteryLevel() = 0;
};

#endif // IPOWER_PORT_H
