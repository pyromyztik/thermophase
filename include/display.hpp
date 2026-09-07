#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <lvgl.h>
#include "config.hpp"
#include "sensor.hpp"
#include "ui.hpp"
#include "wheel.hpp"

// Hardware SPI TFT instance
extern Adafruit_ILI9341 tft;

// Display and LVGL initialization
bool initDisplay();
bool checkDisplayConnected();
void showBootScreen(const char* statusText, uint8_t progressPercent);
void displayTask(void *pvParameters);

// Runtime display and sensor setting updates
void updateCameraRotation(int rotation);
void updateTempUnit(TempUnit unit);
void updatePalette(PaletteType palette);
void updateMirror(bool mirror);
void updateScalingMode(ScalingMode mode);
ScalingMode getScalingMode();
void setDisplayBrightness(uint8_t percent);
uint8_t getDisplayBrightness();

// Buffer and palette accessors for thermal pipeline
uint16_t* getThermalFrameBuffer();
const uint16_t* getActivePalette();
