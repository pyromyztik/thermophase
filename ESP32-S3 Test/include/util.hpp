#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include "esp_chip_info.h"
#include "esp_system.h"

#define RGB_PIN 48 // on-board RGB LED.
#define NUM_PIXELS 1

// Declare the NeoPixel strip object (defined in main.cpp)
extern Adafruit_NeoPixel onboardLED;

// declare tasks
void ledAnimationTask(void *pvParameters);
void diagnosticsMessageTask(void *pvParameters);

// Global task handles (defined in main.cpp)
extern TaskHandle_t diagTaskHandle;