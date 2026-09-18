#pragma once
#include <Arduino.h>

// Initializes battery ADC reading pin and primes 30-sample rolling window
void initBatteryMonitor();

// Re-primes the rolling window and sets percentage once under full system load
void reprimeBatteryUnderLoad();

// Samples instantaneous ADC and appends to the 30-second rolling window
void sampleBattery();

// Reads and returns current 30-second average battery voltage in Volts
float getBatteryVoltage();

// Reads and returns instantaneous battery voltage (for immediate disconnect detection)
float getBatteryInstantVoltage();

// Battery operational states
enum BatteryState {
    BATTERY_DISCONNECTED = 0, // Battery disconnected via switch / powered by USB
    BATTERY_CHARGING,         // Actively charging (USB plugged into TP4056)
    BATTERY_DISCHARGING       // Normal battery operation
};

// Returns the detected battery operational state
BatteryState getBatteryState();
BatteryState updateBatteryState();

// Reads and returns estimated battery percentage (0 - 100%)
// Strictly monotonic (can only decrease when not charging)
uint8_t getBatteryPercentage();

// Comprehensive diagnostic telemetry snapshot
struct BatteryTelemetry {
    float instantV;
    float avgV;
    float step;
    float drift;
    BatteryState state;
    uint8_t percentage;
    uint8_t chargeDebounce;
    uint8_t dischargeDebounce;
    const char* candidate;
    const char* reason;
};

void getBatteryTelemetry(BatteryTelemetry& out);

// Prepares display and peripherals, and enters ESP32-S3 Deep Sleep (~10 uA)
// Wakes up on HW-040 button click (GPIO 6 pulled LOW)
void enterDeepSleep();

// Initializes power/wake state on boot (de-inits RTC GPIO if woken from deep sleep)
void powerWakeInit();
