#include "nrf52/PowerManager.h"
#include "common/FirmwareConfig.h"
#include "common/Logging.h"
#include <Arduino.h>
static const char *TAG = "Power";

// nRF52 power management includes
#ifdef ARDUINO_ARCH_NRF52
#include "nrf_power.h"
#include "nrf_gpio.h"
#include "nrf_soc.h"
#endif

bool PowerManager::configurePowerManagement()
{
    // Configure ADC for battery monitoring
    // Based on Adafruit nRF52 reference implementation
    #if defined(BATTERY_SENSE_RESOLUTION_BITS)
        analogReadResolution(BATTERY_SENSE_RESOLUTION_BITS);
        LOG_I(TAG, "ADC resolution set to %d bits", BATTERY_SENSE_RESOLUTION_BITS);
    #else
        analogReadResolution(PowerConstants::ADC_RESOLUTION_BITS);
    #endif
    // Set analog reference to 3.0V (0.6V ref × 5 = 3.0V max range)
    // This provides better accuracy for LiPo batteries (3.0V-4.2V range after voltage divider)
    analogReference(AR_INTERNAL_3_0);

#ifdef BATTERY_ADC_CTRL
    // Initialize VBAT_ENABLE to HIGH (disabled state) to save power
    // Enable only when reading battery voltage
    // Based on Meshtastic: ADC_CTRL_ENABLED defines the active state
    pinMode(BATTERY_ADC_CTRL, OUTPUT);
    #if defined(BATTERY_ADC_CTRL_ENABLED)
        digitalWrite(BATTERY_ADC_CTRL, !BATTERY_ADC_CTRL_ENABLED); // Opposite of enabled = disabled
    #else
        digitalWrite(BATTERY_ADC_CTRL, HIGH); // Fallback: HIGH = disabled
    #endif
    LOG_I(TAG, "Battery ADC control pin (GPIO %d) initialized to disabled", BATTERY_ADC_CTRL);
#endif

#ifdef ARDUINO_ARCH_NRF52
    // Enable DC/DC converter for better power efficiency
    // This can reduce power consumption by 20-30%
    if (nrf_power_dcdcen_get(NRF_POWER) == false)
    {
        nrf_power_dcdcen_set(NRF_POWER, true);
        LOG_I(TAG, "DC/DC regulator enabled (20-30%% power savings)");
    }
    else
    {
        LOG_I(TAG, "DC/DC regulator already enabled");
    }

    // Configure fast charging (100mA) on BQ25101 chip
    // PIN_CHARGING_CURRENT is defined in variant.h as pin 22 (P0.13 = HICHG signal)
    // LOW = 100mA (fast charge), HIGH = 0mA (disabled), not set = 50mA (default)
    pinMode(PIN_CHARGING_CURRENT, OUTPUT);
    digitalWrite(PIN_CHARGING_CURRENT, LOW);
    LOG_I(TAG, "Battery fast charging enabled (100mA)");
#endif

    LOG_I(TAG, "PowerManager initialized");
    return true;
}

void PowerManager::batteryAdcEnable()
{
#ifdef BATTERY_ADC_CTRL
    // Enable battery voltage divider
    // Based on Meshtastic: Use ADC_CTRL_ENABLED value
    pinMode(BATTERY_ADC_CTRL, OUTPUT);
    #if defined(BATTERY_ADC_CTRL_ENABLED)
        digitalWrite(BATTERY_ADC_CTRL, BATTERY_ADC_CTRL_ENABLED);
    #else
        digitalWrite(BATTERY_ADC_CTRL, LOW); // Fallback: LOW = enabled
    #endif
    delay(10); // Wait for voltage to stabilize
#endif
}

void PowerManager::batteryAdcDisable()
{
#ifdef BATTERY_ADC_CTRL
    // Disable battery voltage divider to save power
    // Based on Meshtastic: Use opposite of ADC_CTRL_ENABLED
    #if defined(BATTERY_ADC_CTRL_ENABLED)
        digitalWrite(BATTERY_ADC_CTRL, !BATTERY_ADC_CTRL_ENABLED);
    #else
        digitalWrite(BATTERY_ADC_CTRL, HIGH); // Fallback: HIGH = disabled
    #endif
#endif
}

uint8_t PowerManager::voltageToPercentage(uint16_t voltagePerCellMv)
{
    // OCV lookup table from FirmwareConfig
    extern const uint16_t OCV[];
    extern const int NUM_OCV_POINTS;

    float battery_SOC = 0.0;

    // Find the OCV range and interpolate
    for (int i = 0; i < NUM_OCV_POINTS; i++)
    {
        if (OCV[i] <= voltagePerCellMv)
        {
            if (i == 0)
            {
                battery_SOC = 100.0; // Fully charged
            }
            else
            {
                // Linear interpolation between OCV[i] and OCV[i-1]
                battery_SOC = (float)100.0 / (NUM_OCV_POINTS - 1.0) *
                              (NUM_OCV_POINTS - 1.0 - i +
                               ((float)voltagePerCellMv - OCV[i]) / (OCV[i - 1] - OCV[i]));
            }
            break;
        }
    }

    // Clamp to 0-100 range
    if (battery_SOC > 100.0)
        battery_SOC = 100.0;
    if (battery_SOC < 0.0)
        battery_SOC = 0.0;

    return (uint8_t)battery_SOC;
}

uint16_t PowerManager::readBatteryVoltage()
{
    const uint32_t BATTERY_SENSE_SAMPLES = 10;

    // Enable battery ADC (set VBAT_ENABLE LOW on Seeed XIAO)
    batteryAdcEnable();

    // Let the ADC settle after enabling the voltage divider
    delay(1);

    // Read battery voltage from ADC with averaging
    // XIAO nRF52840: Battery voltage is divided by ~3.0 via hardware voltage divider
    uint32_t adcSum = 0;
    for (uint32_t i = 0; i < BATTERY_SENSE_SAMPLES; i++)
    {
        adcSum += analogRead(BATTERY_ADC_PIN);
    }
    int adcValue = adcSum / BATTERY_SENSE_SAMPLES;

    // Disable battery ADC to save power (set VBAT_ENABLE HIGH)
    batteryAdcDisable();

    // Convert ADC value to voltage in millivolts
    // Based on Adafruit nRF52 reference implementation
    #if defined(BATTERY_SENSE_RESOLUTION_BITS)
        const int adc_max_value = (1 << BATTERY_SENSE_RESOLUTION_BITS) - 1;
    #else
        const int adc_max_value = 4095; // 12-bit default
    #endif

    // nRF52840 with AR_INTERNAL_3_0: 0.6V internal reference × 5 = 3.0V max range
    // Formula: voltage_mv = raw_adc × VBAT_DIVIDER_COMP × (ADC_MAX_VOLTAGE / (2^resolution))
    // VBAT_MV_PER_LSB = ADC_MAX_VOLTAGE / (2^resolution) = 3000 / 4096 = 0.73242188
    // Seeed XIAO has ~1.5kΩ / 510Ω divider ≈ 3.0 compensation factor
    const float VBAT_MV_PER_LSB = (PowerConstants::ADC_MAX_VOLTAGE * 1000.0) / ((float)(adc_max_value + 1));
    float voltage_mv = adcValue * PowerConstants::BATTERY_DIVIDER * VBAT_MV_PER_LSB;

    // Log battery readings for debugging (every read during debug)
    uint8_t percentage = voltageToPercentage((uint16_t)voltage_mv);
    LOG_I(TAG, "Battery: adc=%d/%d, voltage=%u mV, percentage=%u%%",
          adcValue, adc_max_value, (uint16_t)voltage_mv, percentage);

    return (uint16_t)voltage_mv;
}

uint8_t PowerManager::readBatteryLevel()
{
    uint16_t voltage = readBatteryVoltage();

    // Check for no battery condition
    extern const uint16_t OCV[];
    extern const int NUM_OCV_POINTS;
    const uint16_t MIN_BATTERY_VOLTAGE = OCV[NUM_OCV_POINTS - 1] - 500;

    if (voltage < MIN_BATTERY_VOLTAGE)
    {
        return 0; // No battery or critically low
    }

    // Convert voltage to percentage using OCV lookup table
    return voltageToPercentage(voltage);
}

void PowerManager::enterDeepSleep()
{
    LOG_I(TAG, "Entering deep sleep (System OFF)...");

    // 2. Turn off LEDs (Active LOW)
    // Seeed XIAO nRF52840 has Red/Blue/Green LEDs
    // Green is LED_PIN, but Red/Blue might be on.
    // Drive them HIGH to turn OFF.
    pinMode(LED_RED, OUTPUT);
    digitalWrite(LED_RED, HIGH);

    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_BLUE, HIGH);

    // 3. Flush logs before sleep
    Serial.flush();
    delay(100); // Short delay to allow flush

#ifdef ARDUINO_ARCH_NRF52
    // Configure Wakeup Pins
    // NOTE: nrf_gpio_cfg_sense_input expects PHYSICAL pin numbers.
    // Use g_ADigitalPinMap to map Arduino pin numbers to physical pins.
    uint32_t pinButton = g_ADigitalPinMap[WAKE_BUTTON];
    uint32_t pinLoRa = g_ADigitalPinMap[LORA_DIO0];

    // Configure LoRa DIO0 interrupt pin as wake-up source (Wake on HIGH)
    nrf_gpio_cfg_sense_input(pinLoRa, NRF_GPIO_PIN_PULLDOWN, NRF_GPIO_PIN_SENSE_HIGH);
    LOG_I(TAG, "Wake source: LoRa DIO0 (Pin %d) - wake on HIGH", pinLoRa);

    // Configure wake button as wake-up source (Wake on LOW)
    nrf_gpio_cfg_sense_input(pinButton, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
    LOG_I(TAG, "Wake source: Wake button (Pin %d) - wake on LOW", pinButton);

    // Disable Serial to save power
    Serial.end();

    // Enter System OFF mode
    // This shuts down the CPU and most peripherals.
    // GPIOs are latched or go High-Z depending on configuration.
    uint8_t sd_en;
    (void)sd_softdevice_is_enabled(&sd_en);

    if (sd_en)
    {
        sd_power_system_off();
    }
    else
    {
        NRF_POWER->SYSTEMOFF = 1;
        __DSB(); // Data Synchronization Barrier
    }

#else
    LOG_W(TAG, "System ON sleep mode only available on nRF52");
#endif
}

void PowerManager::printWakeupReason()
{
#ifdef ARDUINO_ARCH_NRF52
    uint32_t resetReason = NRF_POWER->RESETREAS;

    // This was an actual reset (power-on, watchdog, etc.)
    LOG_I(TAG, "nRF52 Reset Reason: 0x%X", resetReason);

    // Map of reset reason flags to human-readable strings
    struct ResetReasonEntry
    {
        uint32_t mask;
        const char *msg;
    };

    ResetReasonEntry reasons[] = {
        {POWER_RESETREAS_RESETPIN_Msk, "  - Reset Pin"},
        {POWER_RESETREAS_DOG_Msk, "  - Watchdog Timeout"},
        {POWER_RESETREAS_SREQ_Msk, "  - Soft Reset (e.g., from NVIC_SystemReset)"},
        {POWER_RESETREAS_LOCKUP_Msk, "  - CPU Lockup"},
        {POWER_RESETREAS_OFF_Msk, "  - System OFF Wakeup (e.g., GPIO, LPCOMP)"},
        {POWER_RESETREAS_LPCOMP_Msk, "  - LPCOMP Wakeup"},
    };

    for (const auto &entry : reasons)
    {
        if (resetReason & entry.mask)
        {
            LOG_I(TAG, "%s", entry.msg);
        }
    }

#else
    LOG_W(TAG, "Wakeup reason reporting only available on nRF52");
#endif
}

bool PowerManager::isLoRaWakeup()
{
#ifdef ARDUINO_ARCH_NRF52
    // Check if we woke from System OFF
    if (NRF_POWER->RESETREAS & POWER_RESETREAS_OFF_Msk)
    {
        // Check if LoRa DIO0 is High (it triggered the wake)
        // Note: Pin mode is default (INPUT) on reset, which is what we want
        // to detect the external high signal.
        return digitalRead(LORA_DIO0) == HIGH;
    }
#endif
    return false;
}