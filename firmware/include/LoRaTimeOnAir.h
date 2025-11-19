#ifndef LORA_TIME_ON_AIR_H
#define LORA_TIME_ON_AIR_H

#include <cstdint>
#include <cmath>
#include <esp_log.h>
#include <algorithm>

namespace LoRaTimeOnAir
{

    /**
     * @brief Calculates the Time on Air (ToA) for a LoRa packet.
     *
     * This function is based on the formulas provided in the Semtech datasheets.
     *
     * @param spreadingFactor The spreading factor (7-12).
     * @param bandwidth The bandwidth in Hz (e.g., 125000).
     * @param codingRate The coding rate (1-4, corresponding to 4/5 to 4/8).
     * @param preambleLength The number of preamble symbols.
     * @param payloadLength The length of the payload in bytes.
     * @param explicitHeader True if an explicit header is used, false for implicit.
     * @param crcEnabled True if CRC is enabled.
     * @param lowDataRateOptimize True if low data rate optimization is enabled.
     * @return The Time on Air in milliseconds.
     */
    static inline double calculateToA_ms(
        uint8_t spreadingFactor,
        double bandwidth,
        uint8_t codingRate,
        uint16_t preambleLength,
        uint8_t payloadLength,
        bool explicitHeader = true,
        bool crcEnabled = true,
        bool lowDataRateOptimize = false)
    {
        // Symbol duration
        double t_sym = std::pow(2, spreadingFactor) / (bandwidth / 1000.0);

        // Preamble duration
        double t_preamble = (preambleLength + 4.25) * t_sym;

        // Payload number of symbols
        int8_t header = explicitHeader ? 0 : 1;
        int8_t crc = crcEnabled ? 16 : 0;
        int8_t de = lowDataRateOptimize ? 1 : 0;

        double payload_numerator = 8.0 * payloadLength - 4.0 * spreadingFactor + 28.0 + crc - 20.0 * header;
        double payload_denominator = 4.0 * (spreadingFactor - 2.0 * de);

        double n_payload = 8.0 + std::max(0.0, std::ceil(payload_numerator / payload_denominator) * (codingRate + 4.0));

        // Payload duration
        double t_payload = n_payload * t_sym;

        // Total ToA
        return t_preamble + t_payload;
    }

} // namespace LoRaTimeOnAir

#endif // LORA_TIME_ON_AIR_H
