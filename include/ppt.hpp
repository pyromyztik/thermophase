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

// Pulsed Phase Thermography (PPT) Spectral Structures for Batch Extraction & BLE Streaming
struct PPTPhasegram {
    int harmonicBin;                      // 1 to 16
    float frequencyHz;                    // f_n = n * 0.25 Hz (0.25 Hz to 4.00 Hz)
    const char* diffusionDepth;           // "Deep", "Mid-Deep", "Medium", "Shallow"
    float minPhaseRad;                    // Minimum phase in radians
    float maxPhaseRad;                    // Maximum phase in radians
    float centerPhaseRad;                 // Optical center phase in radians
    float rawPhaseGrid[PPT_PIXELS];       // Raw IEEE-754 32-bit float phase in radians [-pi, +pi]
    uint8_t normPhaseGrid[PPT_PIXELS];    // 8-bit dynamic contrast-stretched scalar field [0, 255]
    uint32_t crc32;                       // IEEE 802.3 CRC32 checksum over raw & norm data
};

struct PPTSpectralSession {
    uint32_t sessionTimestamp;            // Millis timestamp at acquisition lock
    uint8_t totalPhasegrams;              // 16 harmonic interrogation frequencies
    float baselines[PPT_PIXELS];          // Precalculated reference baseline in PSRAM (zero stack usage)
    char dataLine[1100];                  // PSRAM-backed atomic buffer for "[PPT_PHASEGRAM_DATA] <b64>\r\n"
    PPTPhasegram phasegrams[PPT_MAX_BIN]; // Pre-computed spectral phasegrams in PSRAM
};

// Batch Goertzel DFT Spectral Extraction across all 16 harmonic frequencies upfront
bool pptExtractAllPhasegrams(PPTSpectralSession* session);
PPTSpectralSession* pptGetActiveSession();
