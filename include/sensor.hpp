#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>
#include "config.hpp"

// Thermal frame data structure
struct ThermalFrame {
    float temperatures[SENSOR_PIXELS];
    float minTemp;
    float maxTemp;
    float robustMin;
    float robustMax;
    float centerTemp;
    uint32_t frameIndex;
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

// MLX90640 Hardware Sensor object
extern Adafruit_MLX90640 mlx;

// Sensor initialization and background task
bool initThermalSensor();
void thermalSensorTask(void *pvParameters);
void powerOffSensor();
void powerOnSensor();

// I2C Clock speed configuration
void setSensorI2CSpeed(uint32_t speedHz);
uint32_t getSensorI2CSpeed();

// Sensor noise and bad pixel correction
void cleanSensorOutliers(float* temps);
void filterSnapshotFrame(const float* src, float* dst);
void filterPeakPreservingMedian(const float* src, float* dst, float peakThreshold = 2.5f);
void setLiveDenoiseEnabled(bool enabled);
bool getLiveDenoiseEnabled();

// Radiometric Emissivity Compensation
void applyEmissivityCompensation(float* temps, size_t count, float emissivity, float reflectedTempC);
