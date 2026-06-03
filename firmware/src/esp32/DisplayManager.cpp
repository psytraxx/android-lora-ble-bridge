#include "esp32/DisplayManager.h"

#ifdef ENABLE_OLED_DISPLAY

#include <Wire.h>
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "common/Logging.h"

static const char *TAG = "Display";

// 64x32 SSD1306 OLED, I2C address 0x3C, hardware reset via OLED_RST_PIN build flag
static Adafruit_SSD1306 display(64, 32, &Wire, OLED_RST_PIN);

bool DisplayManager::init()
{
    // VEXT (active LOW) must be driven low to power the display
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, LOW);
    delay(50); // Wait for display power to stabilise

    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    {
        LOG_E(TAG, "SSD1306 not found at 0x3C");
        return false;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 12); // vertically centre "Starting..." on 32px display
    display.print("Starting...");
    display.display();

    LOG_I(TAG, "Display initialised (64x32 SSD1306)");
    return true;
}

void DisplayManager::update(bool bleConnected, int rssi, float snr, uint8_t batteryPct)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // 64x32 display, font 6x8 px → up to 10 chars/line, 4 lines (y=0,8,16,24)
    display.setCursor(0, 0);
    display.print(bleConnected ? "BLE: ON" : "BLE: --");

    display.setCursor(0, 8);
    char buf[16];
    snprintf(buf, sizeof(buf), "RSSI:%4d", rssi);
    display.print(buf);

    display.setCursor(0, 16);
    snprintf(buf, sizeof(buf), "SNR: %4.1f", snr);
    display.print(buf);

    display.setCursor(0, 24);
    snprintf(buf, sizeof(buf), "BAT:  %3d%%", batteryPct);
    display.print(buf);

    display.display();
}

void DisplayManager::clear()
{
    display.clearDisplay();
    display.display();
}

#endif // ENABLE_OLED_DISPLAY
