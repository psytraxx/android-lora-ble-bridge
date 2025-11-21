#ifndef NRF52_LORA_ADAPTER_H
#define NRF52_LORA_ADAPTER_H

#include "ports/ILoRaPort.h"
#include "nrf52/LoRaManager.h"

/**
 * @brief nRF52 LoRa Adapter
 *
 * Wraps LoRaManager (RadioLib with SX1262)
 */
class NRF52LoRaAdapter : public ILoRaPort
{
public:
    NRF52LoRaAdapter(int sck, int miso, int mosi, int ss, int rst, int dio0, int busy)
        : loraManager(sck, miso, mosi, ss, rst, dio0, busy) {}

    bool begin(const LoRaConfig &config) override
    {
        return loraManager.begin(config);
    }

    bool startReceive(bool dutyCycle) override
    {
        return loraManager.startReceive(dutyCycle);
    }

    bool startTransmit(const uint8_t *data, size_t len) override
    {
        return loraManager.startTransmit(data, len);
    }

    void process() override
    {
        loraManager.process();
    }

    void setReceiveCallback(void (*callback)(const LoRaPacket &packet)) override
    {
        loraManager.setReceiveCallback(callback);
    }

    void setTransmitCallback(void (*callback)(bool success)) override
    {
        loraManager.setTransmitCallback(callback);
    }

private:
    LoRaManager loraManager;
};

#endif // NRF52_LORA_ADAPTER_H
