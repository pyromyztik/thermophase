#pragma once
#include <Arduino.h>
#include "config.hpp"

// PPT Spatiotemporal Volume Parameters
#define PPT_NUM_FRAMES        64
#define PPT_RING_FRAMES       8
#define PPT_POST_FRAMES       56
#define PPT_PIXELS            SENSOR_PIXELS   // 32 x 24 = 768

// PPT Frequency Analysis Parameters
#define PPT_MIN_BIN           1               // 0.25 Hz (deepest penetration)
#define PPT_MAX_BIN           16              // 4.00 Hz (shallowest surface voids)
#define PPT_DEFAULT_BIN       4               // 1.00 Hz (medium depth)
#define PPT_SAMPLE_RATE_HZ    16.0f
#define PPT_TRIGGER_THRESH_C  5.0f            // Rate-of-rise threshold in Celsius

// PPT Operating Modes
enum PPTMode {
    PPT_MODE_IDLE = 0,
    PPT_MODE_ARMED,
    PPT_MODE_ACQUIRING,
    PPT_MODE_INSPECT
};

// Core Lifecycle & Initialization
bool initPPT();

// Mode Control
void pptArm();
void pptDisarm();
void pptTrigger(); // Manual pulse trigger
PPTMode getPPTMode();
bool isPPTArmed();
bool isPPTAcquiring();
bool isPPTInspect();

// Acquisition Telemetry & Ingestion (called from Core 0 sensor task)
void pptProcessSensorFrame(const float* currentTemps);
uint8_t pptGetAcquiredFrames();
float pptGetCurrentDt();

// Frequency & Phase Inspection (called from Core 1 display task)
void pptComputePhaseMap(int binK);
void pptSetBin(int binK);
void pptStepBin(int delta);
int pptGetBin();
float pptGetBinFrequency(int binK);
const char* pptGetDepthDescription(int binK);

// Phase Grid Accessors
const float* pptGetPhaseGrid();           // Floating-point phase in radians [-pi, +pi]
const float* pptGetNormalizedPhaseGrid(); // 0.0f - 255.0f normalized for spatial interpolation
void pptGetPhaseStats(float& outMin, float& outMax, float& outCenter);

