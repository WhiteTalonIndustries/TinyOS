// Setup for the TTGO TM (Music) ESP32 board with 2.4" ST7789V display
#define USER_SETUP_ID 23

// See SetupX_Template.h for all options available

#define ST7789_DRIVER

// #define TFT_SDA_READ // Read from display, it only provides an SDA pin

// Generic Pico
#define TFT_SPI_PORT 1 // Set to 0 if SPI0 pins are used, or 1 if spi1 pins used
#define TFT_MISO 12    // Must be defined even though it is not used
#define TFT_MOSI 11    // Connected to display SDA line
#define TFT_SCLK 10
#define TFT_CS   9 // Not connected
#define TFT_DC   8
#define TFT_RST  15  // Connect reset to ensure display initialises
#define TFT_BL   13  // LED back-light
 #define TFT_BACKLIGHT_ON HIGH  // Level to turn ON back-light (HIGH or LOW)

#define TOUCH_CS 16

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// #define TFT_RGB_ORDER TFT_RGB  // Colour order Red-Green-Blue
#define TFT_RGB_ORDER TFT_BGR // Colour order Blue-Green-Red

// Black and white swap option
#define TFT_INVERSION_ON
// #define TFT_INVERSION_OFF

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY  40000000     // This display also seems to work reliably at 80MHz
//#define SPI_FREQUENCY  80000000

#define SPI_READ_FREQUENCY  6000000 // 6 MHz is the maximum SPI read speed for the ST7789V

// The XPT2046 requires a lower SPI clock rate of 2.5MHz so we define that here:
#define SPI_TOUCH_FREQUENCY  2500000
