#pragma once
#include <Arduino.h>
#include "config.hpp"

// Thermal frame data structure
struct ThermalFrame {
    float temperatures[SENSOR_PIXELS];
    float minTemp;
    float maxTemp;
    float centerTemp;
    uint32_t frameIndex;
};

// Bilinear scaling precomputed coefficients
struct ColInterp {
    uint8_t x0;
    uint8_t x1;
    float fx;
};

struct RowInterp {
    uint8_t y0;
    uint8_t y1;
    float fy;
};

// Global task handles
extern TaskHandle_t sensorTaskHandle;
extern TaskHandle_t displayTaskHandle;
extern TaskHandle_t diagTaskHandle;
extern TaskHandle_t ledTaskHandle;

// Shared thread-safe frame buffer access
void postThermalFrame(const ThermalFrame& frame);
bool getLatestThermalFrame(ThermalFrame& outFrame);

