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

// Capture reset reason early before Arduino core clears it
// This constructor runs before setup() and preserves the reset reason
struct ResetReasonCapture
{
    uint32_t reason;
    ResetReasonCapture()
    {
        reason = NRF_POWER->RESETREAS;
        NRF_POWER->RESETREAS = 0xFFFFFFFF; // Clear all bits by writing 1s
    }
};
static ResetReasonCapture g_resetCapture;

// RESETREAS bit definitions
#define RESETREAS_OFF_MASK (1 << 16) // Bit 16: Wake from System OFF
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
    // Same reference as Meshtastic's nRF52 platform: AR_INTERNAL = 0.6V ref × 6 = 3.6V full scale
    analogReference(AR_INTERNAL);

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
    // Mirrors Meshtastic's AnalogBatteryLevel::getBattVoltage() (src/Power.cpp)
    const uint32_t BATTERY_SENSE_SAMPLES = 15;
    const uint32_t MIN_READ_INTERVAL_MS = 5000;

    static bool initialReadDone = false;
    static float lastReadValue = OCV[NUM_OCV_POINTS - 1] * NUM_CELLS;
    static uint32_t lastReadTimeMs = 0;

    // Do not call analogRead() often
    if (initialReadDone && (millis() - lastReadTimeMs < MIN_READ_INTERVAL_MS))
    {
        return (uint16_t)lastReadValue;
    }
    lastReadTimeMs = millis();

    // Enable VBAT divider (VBAT_ENABLE LOW on Seeed XIAO); includes 10ms settle delay
    batteryAdcEnable();

    uint32_t raw = 0;
    for (uint32_t i = 0; i < BATTERY_SENSE_SAMPLES; i++)
    {
        raw += analogRead(BATTERY_ADC_PIN);
    }
    raw = raw / BATTERY_SENSE_SAMPLES;

    batteryAdcDisable();

#if defined(BATTERY_SENSE_RESOLUTION_BITS)
    const int resolutionBits = BATTERY_SENSE_RESOLUTION_BITS;
#else
    const int resolutionBits = PowerConstants::ADC_RESOLUTION_BITS;
#endif

    // scaled = multiplier × (1000 × AREF / 2^bits) × raw
    // XIAO divider: R17=1M / R18=510k → multiplier 3
    float scaled = PowerConstants::BATTERY_DIVIDER *
                   ((1000.0f * PowerConstants::ADC_MAX_VOLTAGE) / (float)(1 << resolutionBits)) * raw;

    if (!initialReadDone)
    {
        // Flush the smoothing filter with the first reading if plausible
        if (scaled > lastReadValue)
            lastReadValue = scaled;
        initialReadDone = true;
    }
    else
    {
        lastReadValue += (scaled - lastReadValue) * 0.5f; // Virtual LPF
    }

    LOG_D(TAG, "Battery: raw=%lu, scaled=%u mV, filtered=%u mV",
          (unsigned long)raw, (uint16_t)scaled, (uint16_t)lastReadValue);

    return (uint16_t)lastReadValue;
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

    pinMode(LED_GREEN, OUTPUT);
    digitalWrite(LED_GREEN, HIGH);

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

    // Enter System OFF mode (does not return)
    // Reset reason will be captured by g_resetCapture on next boot
    sd_power_system_off();

#else
    LOG_W(TAG, "System ON sleep mode only available on nRF52");
#endif
}

void PowerManager::printWakeupReason()
{
#ifdef ARDUINO_ARCH_NRF52
    uint32_t resetReason = g_resetCapture.reason;

    LOG_I(TAG, "Reset reason: 0x%08lX", (unsigned long)resetReason);

    if (resetReason & RESETREAS_OFF_MASK)
    {
        LOG_I(TAG, "Wakeup: System OFF (deep sleep wake)");
    }
    else if (resetReason == 0)
    {
        LOG_I(TAG, "Wakeup: Unknown (register already cleared)");
    }
    else
    {
        LOG_I(TAG, "Wakeup: Reset (not from deep sleep)");
        if (resetReason & (1 << 0))
            LOG_I(TAG, "  - Pin reset");
        if (resetReason & (1 << 1))
            LOG_I(TAG, "  - Watchdog");
        if (resetReason & (1 << 2))
            LOG_I(TAG, "  - Soft reset");
        if (resetReason & (1 << 3))
            LOG_I(TAG, "  - CPU lockup");
    }
#else
    LOG_W(TAG, "Wakeup reason reporting only available on nRF52");
#endif
}

bool PowerManager::isLoraWakeUp()
{
#ifdef ARDUINO_ARCH_NRF52
    // Check if wakeup was from System OFF mode (bit 16 of RESETREAS)
    uint32_t resetReason = g_resetCapture.reason;
    return (resetReason & RESETREAS_OFF_MASK) != 0;
#endif
    return false;
}
