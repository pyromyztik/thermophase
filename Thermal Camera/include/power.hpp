#pragma once
#include <Arduino.h>

// Prepares display and peripherals, and enters ESP32-S3 Deep Sleep (~10 uA)
// Wakes up on HW-040 button click (GPIO 6 pulled LOW)
void enterDeepSleep();

// Initializes power/wake state on boot (de-inits RTC GPIO if woken from deep sleep)
void powerWakeInit();
