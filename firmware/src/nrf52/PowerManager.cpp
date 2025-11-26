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
    analogReadResolution(PowerConstants::ADC_RESOLUTION_BITS);
    analogReference(AR_INTERNAL); // Use internal 0.6V reference

#ifdef BATTERY_ADC_CTRL
    // Initialize VBAT_ENABLE to HIGH (disabled state) to save power
    // Enable only when reading battery voltage
    pinMode(BATTERY_ADC_CTRL, OUTPUT);
    digitalWrite(BATTERY_ADC_CTRL, HIGH);
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
#endif

    LOG_I(TAG, "PowerManager initialized");
    return true;
}

void PowerManager::battery_adcEnable()
{
#ifdef BATTERY_ADC_CTRL
    // Enable battery voltage divider (set VBAT_ENABLE LOW)
    pinMode(BATTERY_ADC_CTRL, OUTPUT);
    digitalWrite(BATTERY_ADC_CTRL, LOW);
    delay(10); // Wait for voltage to stabilize
#endif
}

void PowerManager::battery_adcDisable()
{
#ifdef BATTERY_ADC_CTRL
    // Disable battery voltage divider to save power (set VBAT_ENABLE HIGH)
    digitalWrite(BATTERY_ADC_CTRL, HIGH);
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
    battery_adcEnable();

    // Read battery voltage from ADC with averaging
    // XIAO nRF52840: Battery voltage may be divided by hardware
    uint32_t adcSum = 0;
    for (uint32_t i = 0; i < BATTERY_SENSE_SAMPLES; i++)
    {
        adcSum += analogRead(BATTERY_ADC_PIN);
    }
    int adcValue = adcSum / BATTERY_SENSE_SAMPLES;

    // Disable battery ADC to save power (set VBAT_ENABLE HIGH)
    battery_adcDisable();

    // Convert ADC value to voltage in millivolts
    // nRF52840: 12-bit ADC (0-4095) with 0.6V internal reference and 1/6 gain
    // Max measurable voltage = 0.6V * 6 = 3.6V at ADC value 4095
    // voltage_mv = (adcValue / 4095.0) * 3600.0 * BATTERY_DIVIDER
    float voltage_mv = (adcValue / 4095.0) * PowerConstants::ADC_MAX_VOLTAGE *
                       PowerConstants::BATTERY_DIVIDER * 1000.0;

    // Log debug info periodically
    static uint32_t last_log = 0;
    if (millis() - last_log > 30000)
    {
        LOG_D(TAG, "Battery: adc=%d, voltage=%u mV",
              adcValue, (uint16_t)voltage_mv);
        last_log = millis();
    }

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

void PowerManager::enterLowPowerMode()
{
    LOG_I(TAG, "Entering low-power mode (System OFF)...");

    // Flush logs before sleep
    Serial.flush();

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

    /*Only for debugging purpose, will not be reached without connected debugger*/
    while (1)
        ;

#else
    LOG_W(TAG, "System ON sleep mode only available on nRF52");
#endif
}

void PowerManager::printWakeupReason()
{
#ifdef ARDUINO_ARCH_NRF52
    uint32_t resetReason = NRF_POWER->RESETREAS;

    // Check if this is an actual reset or just a wakeup from System ON sleep
    if (resetReason == 0)
    {
        // No reset occurred - this is a wakeup from System ON sleep mode
        // Check which GPIO triggered the wakeup by reading the pin states
        bool dio0_high = digitalRead(LORA_DIO0) == HIGH;
        bool button_low = digitalRead(WAKE_BUTTON) == LOW;

        LOG_I(TAG, "Woke from System ON sleep mode:");

        if (dio0_high && button_low)
        {
            LOG_I(TAG, "  - Both LoRa DIO0 and Wake button triggered");
        }
        else if (dio0_high)
        {
            LOG_I(TAG, "  - LoRa DIO0 (GPIO %d) triggered wakeup", LORA_DIO0);
        }
        else if (button_low)
        {
            LOG_I(TAG, "  - Wake button (GPIO %d) triggered wakeup", WAKE_BUTTON);
        }
        else
        {
            LOG_I(TAG, "  - Unknown wakeup source (both pins idle)");
        }
    }
    else
    {
        // This was an actual reset (power-on, watchdog, etc.)
        LOG_I(TAG, "nRF52 Reset Reason: 0x%X", resetReason);

        if (resetReason & POWER_RESETREAS_RESETPIN_Msk)
        {
            LOG_I(TAG, "  - Reset Pin");
        }
        if (resetReason & POWER_RESETREAS_DOG_Msk)
        {
            LOG_I(TAG, "  - Watchdog Timeout");
        }
        if (resetReason & POWER_RESETREAS_SREQ_Msk)
        {
            LOG_I(TAG, "  - Soft Reset (e.g., from NVIC_SystemReset)");
        }
        if (resetReason & POWER_RESETREAS_LOCKUP_Msk)
        {
            LOG_I(TAG, "  - CPU Lockup");
        }
        if (resetReason & POWER_RESETREAS_OFF_Msk)
        {
            LOG_I(TAG, "  - System OFF Wakeup (e.g., GPIO, LPCOMP)");
        }
        if (resetReason & POWER_RESETREAS_LPCOMP_Msk)
        {
            LOG_I(TAG, "  - LPCOMP Wakeup");
        }

        // Clear reset reasons after reading
        NRF_POWER->RESETREAS = 0xFFFFFFFF;
    }

#else
    LOG_W(TAG, "Wakeup reason reporting only available on nRF52");
#endif
}
