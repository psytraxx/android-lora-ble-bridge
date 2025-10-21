#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino_GFX_Library.h> // Include Arduino_GFX library
#define GFX_DEV_DEVICE LILYGO_T_DISPLAY_S3

class DisplayManager
{
public:
    DisplayManager(int dataPin0, int dataPin1, int dataPin2, int dataPin3, int dataPin4, int dataPin5, int dataPin6, int dataPin7,
                   int writePin, int readPin, int dataCommandPin, int chipSelectPin, int resetPin, int backlightPin)
    {
        pinMode(backlightPin, OUTPUT);
        digitalWrite(backlightPin, HIGH);

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

    // Display received LoRa message
    void displayLoRaMessage(const String &message, int rssi, float snr)
    {
        // Clear only the top part for the LoRa message
        int loraInfoHeight = 40; // Adjust based on text size and lines needed
        fillRect(0, 0, width(), loraInfoHeight, BLACK);
        setCursor(0, 0); // Set cursor to top-left
        setTextSize(2);  // Use a larger font for the message
        printLine("Received:");
        printLine(message);
        setTextSize(1); // Smaller font for RSSI/SNR
        print("RSSI: ");
        print(String(rssi));
        print(" dBm");
        print(" / SNR: ");
        printLine(String(snr, 2)); // Print SNR with 2 decimal places
    }

    // Display GPS information at the bottom of the screen
    void displayGPSInfo(const String &gpsInfo)
    {
        // Clear the bottom area for GPS info
        int gpsInfoY = height() - 20; // Start Y position for GPS info
        fillRect(0, gpsInfoY, width(), 20, BLACK);
        setCursor(0, gpsInfoY);
        setTextSize(1);
        setTextColor(GREEN, BLACK); // Use a different color for GPS
        print(gpsInfo);
        setTextColor(WHITE, BLACK); // Reset to default color
    }

    // Display warning message
    void displayWarning(const String &warning)
    {
        int warningY = height() - 30; // Position above GPS line
        if (warning.length() > 0)
        {
            fillRect(0, warningY, width(), 10, BLACK); // Clear area
            setCursor(0, warningY);
            setTextColor(RED, BLACK);
            setTextSize(1);
            print(warning);
            setTextColor(WHITE, BLACK); // Reset color
        }
        else
        {
            fillRect(0, warningY, width(), 10, BLACK); // Clear area
        }
    }

    // Display calculated distance in meters
    void displayDistance(double distanceMeters)
    {
        int distanceY = 60;      // Y position below LoRa message area (adjust as needed)
        int distanceHeight = 30; // Height for the distance text (adjust based on font size)

        // Clear the area for distance display
        fillRect(0, distanceY, width(), distanceHeight, BLACK);
        setCursor(0, distanceY); // Set cursor for distance text

        setTextSize(3); // Use a large font size for distance

        if (distanceMeters >= 0.0)
        {
            print("Dist: ");
            print(String(distanceMeters, 0)); // Print distance as whole meters
            print(" m");                      // Display unit as meters
        }
        else
        {
            print("Dist: N/A"); // Indicate invalid distance
        }

        // Reset text size if needed for subsequent prints
        setTextSize(1);
    }

private:
    Arduino_GFX *gfx; // Pointer to Arduino_GFX object
};

#endif // DISPLAY_MANAGER_H
