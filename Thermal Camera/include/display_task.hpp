#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "config.hpp"
#include "thermal_types.hpp"

// Display driver object
extern Adafruit_ILI9341 tft;

// Function prototypes
bool initDisplay();
void displayTask(void *pvParameters);

