#pragma once
#include <Arduino.h>
#include <Adafruit_MLX90640.h>



// ==========================================================
// PIN CONFIGURATION FOR ESP32-S3 
// ==========================================================

// MLX90640 I2C Pins (as in MLX90640 Test)
#define PIN_I2C_SDA       8
#define PIN_I2C_SCL       9
#define I2C_FREQ_INIT     400000
#define I2C_FREQ_RUN      800000

// ILI9341 SPI Display Pins (as in TFT Display Test)
#define TFT_CS            1   // Chip Select
#define TFT_DC            2   // Data / Command
#define TFT_RST           42  // Hardware Reset
#define TFT_MOSI          41  // SPI MOSI (SDI on display)
#define TFT_SCK           40  // SPI Clock (SCK on display)
#define TFT_MISO          39  // SPI MISO (SDO on display)
#define TFT_LED           21  // Backlight control (PWM)
#define TFT_LED_PWM_CHANNEL  0   // LEDC PWM Channel
#define TFT_LED_PWM_FREQ     5000// 5 kHz eliminates backlight flicker
#define TFT_LED_PWM_RES      8   // 8-bit resolution (0 - 255)
#define TFT_DEFAULT_BRIGHTNESS 80 // Default brightness percentage (10% - 100%)

// MISO / SDO Diagnostic check:
// - Set to true to detect display presence & disconnection via SPI MISO (GPIO 39) register readback.
// - Set to false only if using write-only display wiring (no SDO wire connected).
#define CHECK_TFT_MISO    true

// On-board WS2812 NeoPixel RGB LED (as in ESP32-S3 Test)
#define RGB_PIN           48
#define NUM_PIXELS        1
#define LED_BRIGHTNESS    50  // 0-255

// HW-040 Rotary Encoder Wheel Pins (Consecutive on Left Header J1)
#define PIN_WHEEL_CLK     4   // Rotary CLK (Pin 4 on J1)
#define PIN_WHEEL_DT      5   // Rotary DT  (Pin 5 on J1)
#define PIN_WHEEL_SW      6   // Push Button Switch (Pin 6 on J1)



// ==========================================================
// UI THEME & ACCENT COLOR CONFIGURATION
// ==========================================================
// Primary UI Accent Color (24-bit RGB Hex).
// Changes all active focus borders, selection arrows, sliders, and reticle indicators!
#define UI_ACCENT_COLOR_HEX      0xFF1E27
// Subtle glow / pressed state shade
#define UI_ACCENT_COLOR_DIM_HEX  0x8B0000
// Secondary UI Accent Color (24-bit RGB Hex).
// Changes contextual descriptions, section headers, and sub-labels!
#define UI_SECONDARY_ACCENT_HEX  0xFFD600



// ==========================================================
// THERMAL SENSOR CONFIGURATION
// ==========================================================
#define SENSOR_WIDTH      32
#define SENSOR_HEIGHT     24
#define SENSOR_PIXELS     (SENSOR_WIDTH * SENSOR_HEIGHT)

// Minimum auto-ranging temperature span in Celsius:
// Clamps dynamic range to prevent sensor thermal noise and residual ripple
// from being over-amplified across the palette in low-contrast / flat scenes.
#define THERMAL_MIN_RANGE_SPAN 5.0f

// MLX90640 Refresh Rate:
// NOTE: Adafruit_MLX90640::getFrame() reads TWO subpages (0 & 1) to calculate each full frame.
// - MLX90640_16_HZ -> 16 subpages/s / 2 = ~8 FPS  (matches default MLX90640 rate)
// - MLX90640_32_HZ -> 32 subpages/s / 2 = ~11-13 FPS (used in MLX90640 Test to achieve 10-12 FPS)
// - MLX90640_64_HZ -> 64 subpages/s / 2 = ~16-18 FPS (achieves full 16 FPS) (nope)
#define THERMAL_REFRESH_RATE MLX90640_32_HZ



// ==========================================================
// CAMERA ROTATION & ORIENTATION
// ==========================================================
// Thermal feed rotation options:
// 0 = 0 degrees (original sensor orientation)
// 1 = 90 degrees clockwise
// 2 = 180 degrees (upside down)
// 3 = 270 degrees clockwise (90 degrees counter-clockwise)
#define CAMERA_ROTATION   3

// When rotated 90 or 270 degrees on a 320x240 landscape display:
// - false: Preserves true 3:4 aspect ratio (180x240 centered) with dedicated side panels for HUD & color bar 
// - true : Stretches the rotated 24x32 feed to fill the entire 320x240 screen
#define STRETCH_ROTATED_IMAGE false

// Flip horizontally (mirror) if needed
#define MIRROR_HORIZONTAL false



// ==========================================================
// DISPLAY DIMENSIONS & SPI SPEED
// ==========================================================
#define DISPLAY_WIDTH     320
#define DISPLAY_HEIGHT    240
#define DISPLAY_ROTATION  1   // 1 = Landscape (320x240), 0 = Portrait (240x320)

// SPI Clock for TFT: 40 MHz pushes the full 320x240 frame in ~30 ms, eliminating rolling shutter
#define TFT_SPI_FREQ      40000000



// ==========================================================
// FREERTOS TASK CONFIGURATION
// ==========================================================
#define SENSOR_TASK_CORE     0
#define SENSOR_TASK_PRIO     3
#define SENSOR_TASK_STACK    8192

#define DISPLAY_TASK_CORE    1
#define DISPLAY_TASK_PRIO    2
#define DISPLAY_TASK_STACK   12288

#define LED_TASK_CORE        0
#define LED_TASK_PRIO        1
#define LED_TASK_STACK       2048

#define DIAG_TASK_CORE       1
#define DIAG_TASK_PRIO       1
#define DIAG_TASK_STACK      4096

#define WHEEL_TASK_CORE      0
#define WHEEL_TASK_PRIO      2
#define WHEEL_TASK_STACK     3072
