#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino_GFX_Library.h> // Include Arduino_GFX library
#define GFX_DEV_DEVICE LILYGO_T_DISPLAY_S3

class DisplayManager
{
public:
    DisplayManager(int dataPin0, int dataPin1, int dataPin2, int dataPin3, int dataPin4, int dataPin5, int dataPin6, int dataPin7,
                   int writePin, int readPin, int dataCommandPin, int chipSelectPin, int resetPin, int backlightPin)
        : blPin(backlightPin), currentBrightness(255)
    {
        // Configure PWM for backlight control (ESP32 Arduino 3.x API)
        ledcAttach(backlightPin, 5000, 8); // Pin, 5kHz frequency, 8-bit resolution
        ledcWrite(backlightPin, 255);      // Full brightness initially

        Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(dataCommandPin, chipSelectPin, writePin, readPin,
                                                      dataPin0, dataPin1, dataPin2, dataPin3,
                                                      dataPin4, dataPin5, dataPin6, dataPin7);
        gfx = new Arduino_ST7789(bus, resetPin, backlightPin, true, 170, 320, 35, 0, 35, 0); // Adjust offsets as needed
    }

    /**
     * @brief Initializes the display.
     */
    void setup()
    {
        gfx->begin();
        gfx->setRotation(1); // Adjust rotation as needed (0-3)
        gfx->fillScreen(BLACK);
        gfx->setTextColor(WHITE, BLACK); // Set text color (foreground, background)
        gfx->setTextSize(1);             // Set text size
        gfx->setCursor(0, 0);
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
     * @brief Sets the text size.
     * @param size The text size.
     */
    void setTextSize(int size)
    {
        gfx->setTextSize(size);
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
        ledcWrite(blPin, brightness);
    }

    /**
     * @brief Gets the current backlight brightness.
     * @return Current brightness level (0-255).
     */
    uint8_t getBrightness()
    {
        return currentBrightness;
    }

private:
    Arduino_GFX *gfx;          // Pointer to Arduino_GFX object
    int blPin;                 // Backlight pin
    uint8_t currentBrightness; // Current brightness level
};

#endif // DISPLAY_MANAGER_H
