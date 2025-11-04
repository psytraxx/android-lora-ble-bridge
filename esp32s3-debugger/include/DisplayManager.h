#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h> // Include Arduino_GFX library
#define GFX_DEV_DEVICE LILYGO_T_DISPLAY_S3

class DisplayManager
{
public:
    DisplayManager(int dataPin0, int dataPin1, int dataPin2, int dataPin3, int dataPin4, int dataPin5, int dataPin6, int dataPin7,
                   int writePin, int readPin, int dataCommandPin, int chipSelectPin, int resetPin, int backlightPin)
        : blPin(backlightPin), currentBrightness(255)
    {
        // Configure PWM for backlight control (ESP32 Arduino API)
        // Use a dedicated LEDC channel. ledcAttach(backlightPin, ...) is not part of
        // the ESP32 Arduino API; use ledcSetup + ledcAttachPin instead.
        const int DEFAULT_BL_CH = 0; /* use channel 0 by default */
        blChannel = DEFAULT_BL_CH;
        ledcSetup(blChannel, 5000, 8); // channel, frequency (Hz), resolution (bits)
        ledcAttachPin(backlightPin, blChannel);
        ledcWrite(blChannel, 255); // Full brightness initially

        Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(dataCommandPin, chipSelectPin, writePin, readPin,
                                                      dataPin0, dataPin1, dataPin2, dataPin3,
                                                      dataPin4, dataPin5, dataPin6, dataPin7);
        gfx = new Arduino_ST7789(bus, resetPin, backlightPin, true, 170, 320, 35, 0, 35, 0); // Adjust offsets as needed

        // Save reset pin for power control
        resetPinNum = resetPin;
        pinMode(resetPinNum, OUTPUT);
        digitalWrite(resetPinNum, HIGH); // release reset
    }

    /**
     * @brief Initializes the display.
     */
    void setup()
    {
        // Ensure reset line is high and display is ready
        digitalWrite(resetPinNum, HIGH);
        delay(120); // Give display time to stabilize after power-on

        gfx->begin();
        gfx->setRotation(1); // Adjust rotation as needed (0-3)
        gfx->fillScreen(BLACK);
        gfx->setTextColor(WHITE, BLACK); // Set text color (foreground, background)
        setFontGeneral();                // Set default font for general text
        gfx->setCursor(0, 0);

        // Mark display as powered on
        displayPowered = true;
    }

    /**
     * @brief Clears the screen.
     */
    void clearScreen()
    {
        gfx->fillScreen(BLACK);
        gfx->setCursor(0, 0);
    }

    /**
     * @brief Prints a line of text to the display.
     * @param text The text to print.
     */
    void printLine(const String &text)
    {
        Serial.println(text);
        gfx->println(text);
    }

    /**
     * @brief Prints text to the display.
     * @param text The text to print.
     */
    void print(const String &text)
    {
        gfx->print(text);
    }

    /**
     * @brief Prints C-string to the display.
     * @param text The C-string to print.
     */
    void print(const char *text)
    {
        gfx->print(text);
    }

    /**
     * @brief Prints an integer to the display.
     * @param value The integer value to print.
     */
    void print(int value)
    {
        gfx->print(value);
    }

    /**
     * @brief Prints a float to the display.
     * @param value The float value to print.
     * @param decimals Number of decimal places.
     */
    void print(float value, int decimals = 2)
    {
        gfx->print(value, decimals);
    }

    /**
     * @brief Sets the cursor position.
     * @param x The x coordinate.
     * @param y The y coordinate.
     */
    void setCursor(int x, int y)
    {
        gfx->setCursor(x, y);
    }

    /**
     * @brief Sets the font to tiny size (~8px) for small UI elements/labels.
     */
    void setFontTiny()
    {
        gfx->setTextSize(1);
    }

    /**
     * @brief Sets the font to general size (~16px) for text/menus.
     */
    void setFontGeneral()
    {
        gfx->setTextSize(2);
    }

    /**
     * @brief Sets the text color.
     * @param foreground The foreground color.
     * @param background The background color.
     */
    void setTextColor(uint16_t foreground, uint16_t background)
    {
        gfx->setTextColor(foreground, background);
    }

    /**
     * @brief Fills a rectangle with a specified color.
     * @param x The x coordinate of the top-left corner.
     * @param y The y coordinate of the top-left corner.
     * @param w The width of the rectangle.
     * @param h The height of the rectangle.
     * @param color The color to fill the rectangle with.
     */
    void fillRect(int x, int y, int w, int h, uint16_t color)
    {
        gfx->fillRect(x, y, w, h, color);
    }

    int width()
    {
        return gfx->width();
    }

    int height()
    {
        return gfx->height();
    }

    /**
     * @brief Sets the backlight brightness (0-255).
     * @param brightness The brightness level (0 = off, 255 = full brightness).
     */
    void setBrightness(uint8_t brightness)
    {
        currentBrightness = brightness;
        ledcWrite(blChannel, brightness);
    }

    /**
     * @brief Gets the current backlight brightness.
     * @return Current brightness level (0-255).
     */
    uint8_t getBrightness()
    {
        return currentBrightness;
    }

    /**
     * @brief Power off the display (use reset line and turn backlight off).
     * This explicitly powers down the display controller so nothing is visible
     * even if the backlight edges remain powered. The previous brightness
     * is saved and restored on powerOn().
     */
    void powerOff()
    {
        if (!displayPowered)
            return;
        // Save current brightness so we can restore later
        savedBrightness = currentBrightness;
        setBrightness(0);
        // Assert reset to power down panel/controller
        digitalWrite(resetPinNum, LOW);
        displayPowered = false;
    }

    /**
     * @brief Power on the display (release reset and re-initialize display).
     */
    void powerOn()
    {
        if (displayPowered)
            return;
        // Release reset and give the panel time to initialize
        // Using longer delay for battery operation stability
        digitalWrite(resetPinNum, HIGH);
        delay(120); // Increased from 50ms for better battery operation
        // Re-init the gfx driver to ensure internal state is correct
        gfx->begin();
        gfx->setRotation(1);
        gfx->fillScreen(BLACK);
        setTextColor(WHITE, BLACK);
        setFontGeneral();
        // Restore brightness
        setBrightness(savedBrightness > 0 ? savedBrightness : 255);
        displayPowered = true;
    }

private:
    Arduino_GFX *gfx;          // Pointer to Arduino_GFX object
    int blPin;                 // Backlight pin
    uint8_t currentBrightness; // Current brightness level
    int blChannel;             // LEDC channel used for backlight PWM

    // Reset pin used for hardware power control of the display
    int resetPinNum;
    // Track whether display controller is powered (reset released)
    bool displayPowered = true;
    // Saved brightness for restore after power on
    uint8_t savedBrightness = 255;

    /**
     * @brief Sets the text size.
     * @param size The text size.
     */
    void setTextSize(int size)
    {
        gfx->setTextSize(size);
    }
};

#endif // DISPLAY_MANAGER_H
