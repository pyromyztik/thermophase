#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>
#include "config.hpp"
#include "thermal_types.hpp"

// Sensor object
extern Adafruit_MLX90640 mlx;

// Function prototypes
bool initThermalSensor();
void thermalSensorTask(void *pvParameters);

