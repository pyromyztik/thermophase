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

// System and device connection health statuses
enum DeviceStatus {
    STATUS_INITIALIZING,
    STATUS_OK,
    STATUS_DISPLAY_ERROR,
    STATUS_SENSOR_ERROR
};
enum SensorStatus {
    SENSOR_OK,
    SENSOR_DISCONNECTED,
    SENSOR_ERROR
};

extern volatile DeviceStatus globalDeviceStatus;
extern volatile SensorStatus globalSensorStatus;
extern volatile uint32_t sensorReconnectAttempts;

// Live telemetry values for continuous serial reporting
extern volatile float displayFps;
extern volatile float displayCenterTemp;
extern volatile float displayMinTemp;
extern volatile float displayMaxTemp;

// Global task handles
extern TaskHandle_t sensorTaskHandle;
extern TaskHandle_t displayTaskHandle;
extern TaskHandle_t diagTaskHandle;
extern TaskHandle_t ledTaskHandle;

// Shared thread-safe frame buffer access
void postThermalFrame(const ThermalFrame& frame);
bool getLatestThermalFrame(ThermalFrame& outFrame);

