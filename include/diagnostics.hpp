#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sensor.hpp"

// Real-time Compute & Headroom Telemetry
struct ComputeTelemetry {
    // Core 1: Display, Scaling, LVGL & DMA SPI
    float core1LoadPct;       // % of time Core 1 spends computing per frame cycle
    float scaleTimeMs;        // Execution duration of the active scaling algorithm (ms)
    float flushTimeMs;        // Execution duration of LVGL rendering & SPI DMA flush (ms)
    float frameTotalMs;       // Total active frame processing time on Core 1 (ms)
    float frameBudgetMs;      // Time budget between frames at current FPS (ms)
    float core1HeadroomMs;    // Unused compute headroom per frame (ms)
    float core1HeadroomPct;   // % of Core 1 idle headroom remaining

    // Core 0: Thermal Sensor I2C & Background IO
    float core0LoadPct;       // % of time Core 0 spends reading MLX90640 over I2C
    float sensorReadMs;       // Time spent executing mlx.getFrame() per frame (ms)
    float sensorIntervalMs;   // Time between consecutive sensor frames (ms)
    float core0HeadroomPct;   // % of Core 0 idle headroom remaining
};

extern volatile ComputeTelemetry globalComputeStats;

// Profiler Hooks
void logFrameProfile(int64_t scaleUs, int64_t flushUs, int64_t frameTotalUs, int64_t frameIntervalUs);
void logSensorProfile(int64_t sensorActiveUs, int64_t sensorIntervalUs);

// Comprehensive Compute and Headroom Report
void printComputeHeadroomReport();

// System diagnostics FreeRTOS task
void diagnosticsMessageTask(void *pvParameters);
