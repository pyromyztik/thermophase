#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "esp_chip_info.h"
#include "esp_system.h"
#include "sensor.hpp"

// System diagnostics FreeRTOS task
void diagnosticsMessageTask(void *pvParameters);

