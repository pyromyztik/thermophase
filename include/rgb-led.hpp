#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config.hpp"

// NeoPixel LED object
extern Adafruit_NeoPixel onboardLED;

// Function prototypes
void initRGB();
void turnOffRGB();
void ledAnimationTask(void *pvParameters);

