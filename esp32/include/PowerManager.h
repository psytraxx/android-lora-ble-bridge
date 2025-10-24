#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "esp_pm.h"
#include <Arduino.h>

/**
 * @brief Power management for LoRa transmission
 *
 * This class manages power locks to ensure maximum performance during LoRa TX
 * while allowing the system to enter low-power states during idle/RX periods.
 *
 * Usage:
 * - Call acquireForLoRaTx() before starting LoRa transmission
 * - Call releaseAfterLoRaTx() after transmission completes
 *
 * Power savings:
 * - During RX/idle: CPU can run at 10 MHz, system can enter light sleep
 * - During TX: CPU runs at 80 MHz, light sleep disabled for reliable transmission
 */
class PowerManager
{
public:
    PowerManager() : cpu_freq_lock(nullptr), no_light_sleep_lock(nullptr)
    {
        // Create power management lock for maximum CPU frequency during LoRa TX
        esp_err_t err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "lora_tx", &cpu_freq_lock);
        if (err != ESP_OK)
        {
            Serial.printf("Failed to create CPU freq lock: %d\n", err);
        }

        // Create power management lock to prevent light sleep during LoRa TX
        err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "lora_tx_nosleep", &no_light_sleep_lock);
        if (err != ESP_OK)
        {
            Serial.printf("Failed to create no-light-sleep lock: %d\n", err);
        }

        Serial.println("PowerManager initialized - dynamic power control enabled");
    }

    ~PowerManager()
    {
        if (cpu_freq_lock)
            esp_pm_lock_delete(cpu_freq_lock);
        if (no_light_sleep_lock)
            esp_pm_lock_delete(no_light_sleep_lock);
    }

    /**
     * @brief Acquire locks before LoRa TX
     *
     * Boosts CPU to maximum frequency and prevents light sleep
     * to ensure reliable transmission timing.
     */
    void acquireForLoRaTx()
    {
        if (cpu_freq_lock)
        {
            esp_pm_lock_acquire(cpu_freq_lock);
        }
        if (no_light_sleep_lock)
        {
            esp_pm_lock_acquire(no_light_sleep_lock);
        }
        Serial.println("PM: High power mode for LoRa TX");
    }

    /**
     * @brief Release locks after LoRa TX
     *
     * Allows CPU to scale down to minimum frequency and
     * permits automatic light sleep for power savings.
     */
    void releaseAfterLoRaTx()
    {
        if (no_light_sleep_lock)
        {
            esp_pm_lock_release(no_light_sleep_lock);
        }
        if (cpu_freq_lock)
        {
            esp_pm_lock_release(cpu_freq_lock);
        }
        Serial.println("PM: Released to low power mode");
    }

private:
    esp_pm_lock_handle_t cpu_freq_lock;
    esp_pm_lock_handle_t no_light_sleep_lock;
};

#endif // POWER_MANAGER_H
