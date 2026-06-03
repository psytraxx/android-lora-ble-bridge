#ifndef ESP32_DISPLAY_MANAGER_H
#define ESP32_DISPLAY_MANAGER_H

#ifdef ENABLE_OLED_DISPLAY

#include <stdint.h>

class DisplayManager
{
public:
    // Call once in setup() after power init. Returns false if display not found.
    static bool init();

    // Refresh all display lines. Call whenever BLE state, RSSI/SNR, or battery changes.
    static void update(bool bleConnected, int rssi, float snr, uint8_t batteryPct);

    // Clear the display before deep sleep.
    static void clear();
};

#endif // ENABLE_OLED_DISPLAY
#endif // ESP32_DISPLAY_MANAGER_H
