#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "config.hpp"
#include "thermal_types.hpp"
#include "ui.hpp"
#include "encoder.hpp"

// Display driver object
extern Adafruit_ILI9341 tft;

// Function prototypes
bool initDisplay();
bool checkDisplayConnected();
void showBootScreen(const char* statusText, uint8_t progressPercent);
void displayTask(void *pvParameters);

// Runtime display setting updates
void updateCameraRotation(int rotation);
void updateTempUnit(TempUnit unit);
void updatePalette(PaletteType palette);
void updateMirror(bool mirror);
