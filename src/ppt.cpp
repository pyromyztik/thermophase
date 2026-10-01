#include "ppt.hpp"
#include "sensor.hpp"
#include "ble.hpp"
#include <esp_heap_caps.h>
#include <cmath>

// 3D Spatiotemporal Transient Data Cube (64 frames x 768 pixels, centi-degrees int16_t, ~98.3 KB)
static int16_t (*transient_cube)[PPT_PIXELS] = nullptr;

// Pre-computed Spectral Phasegram Session Buffer (~62 KB)
static PPTSpectralSession* s_activeSession = nullptr;

// Rolling 8-frame ring buffer for pre-trigger transient lock (~12.3 KB)
static int16_t ringBuffer[PPT_RING_FRAMES][PPT_PIXELS];
static int ringHead = 0;
static int ringCount = 0;

// State tracking variables
static volatile PPTMode currentPPTMode = PPT_MODE_IDLE;
static volatile uint8_t acquiredFrames = 0;
static volatile float currentDt = 0.0f;
static volatile bool manualTriggerFlag = false;

// Previous frame temperatures for rate-of-rise (dT/dt) detection
static float prevTemps[PPT_PIXELS];
static bool hasPrevTemps = false;

// Phase map storage
static int currentBinK = PPT_DEFAULT_BIN;
static float phaseGrid[PPT_PIXELS];           // Raw phase angles in radians [-pi, +pi]
static float normalizedPhaseGrid[PPT_PIXELS]; // 0.0f - 255.0f scalar array for spatial upscaler

static float phaseMinVal = -3.14159265f;
static float phaseMaxVal = 3.14159265f;
static float phaseCenterVal = 0.0f;

bool initPPT() {
    Serial.println("[PPT] Initializing Pulsed Phase Thermography engine...");

    // Allocate 3D transient cube in 8MB Octal PSRAM
    if (transient_cube == nullptr) {
        size_t cubeBytes = PPT_NUM_FRAMES * PPT_PIXELS * sizeof(int16_t);
        transient_cube = (int16_t(*)[PPT_PIXELS])heap_caps_malloc(
            cubeBytes, 
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (transient_cube == nullptr) {
            Serial.println("[PPT] Warning: PSRAM alloc failed for transient_cube, falling back to DRAM...");
            transient_cube = (int16_t(*)[PPT_PIXELS])malloc(cubeBytes);
        }
        if (transient_cube == nullptr) {
            Serial.println("[PPT] FATAL: Could not allocate transient cube buffer!");
            return false;
        }
        memset(transient_cube, 0, cubeBytes);
        Serial.printf("[PPT] Transient cube allocated: %u KB in %s\n", 
                      (unsigned int)(cubeBytes / 1024),
                      esp_ptr_external_ram(transient_cube) ? "Octal PSRAM" : "Internal DRAM");
    }

    // Allocate Spectral Phasegram Session Buffer in 8MB Octal PSRAM (~62 KB)
    if (s_activeSession == nullptr) {
        size_t sessionBytes = sizeof(PPTSpectralSession);
        s_activeSession = (PPTSpectralSession*)heap_caps_malloc(
            sessionBytes,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (s_activeSession == nullptr) {
            s_activeSession = (PPTSpectralSession*)malloc(sessionBytes);
        }
        if (s_activeSession != nullptr) {
            memset(s_activeSession, 0, sessionBytes);
            Serial.printf("[PPT] Spectral session buffer allocated: %u KB in %s\n",
                          (unsigned int)(sessionBytes / 1024),
                          esp_ptr_external_ram(s_activeSession) ? "Octal PSRAM" : "Internal DRAM");
        } else {
            Serial.println("[PPT] Warning: Could not allocate spectral session buffer!");
        }
    }

    memset(ringBuffer, 0, sizeof(ringBuffer));
    ringHead = 0;
    ringCount = 0;
    currentPPTMode = PPT_MODE_IDLE;
    acquiredFrames = 0;
    currentDt = 0.0f;
    manualTriggerFlag = false;
    hasPrevTemps = false;
    currentBinK = PPT_DEFAULT_BIN;

    for (int i = 0; i < PPT_PIXELS; ++i) {
        phaseGrid[i] = 0.0f;
        normalizedPhaseGrid[i] = 128.0f;
    }

    Serial.println("[PPT] PPT Engine initialized successfully.");
    return true;
}

void pptArm() {
    if (transient_cube == nullptr) {
        initPPT();
    }
    ringHead = 0;
    ringCount = 0;
    acquiredFrames = 0;
    currentDt = 0.0f;
    manualTriggerFlag = false;
    hasPrevTemps = false;
    currentPPTMode = PPT_MODE_ARMED;
    Serial.println("[PPT] >>> PPT STATE: ARMED (Look-Around Mode: Live preview, click knob to record) <<<");
}

void pptDisarm() {
    currentPPTMode = PPT_MODE_IDLE;
    manualTriggerFlag = false;
    acquiredFrames = 0;
    Serial.println("[PPT] PPT Disarmed (returned to Live Preview)");
}

void pptTrigger() {
    if (currentPPTMode == PPT_MODE_ARMED) {
        manualTriggerFlag = true;
        Serial.println("[PPT] Manual trigger pulse received from HW-040 button click!");
    }
}

PPTMode getPPTMode() {
    return currentPPTMode;
}

bool isPPTArmed() {
    return (currentPPTMode == PPT_MODE_ARMED);
}

bool isPPTAcquiring() {
    return (currentPPTMode == PPT_MODE_ACQUIRING);
}

bool isPPTInspect() {
    return (currentPPTMode == PPT_MODE_INSPECT);
}

uint8_t pptGetAcquiredFrames() {
    return acquiredFrames;
}

float pptGetCurrentDt() {
    return currentDt;
}

void pptProcessSensorFrame(const float* currentTemps) {
    if (transient_cube == nullptr || currentTemps == nullptr) return;

    // Convert float Celsius to centi-degrees int16_t to avoid floating-point heap overhead
    int16_t currentCenti[PPT_PIXELS];
    for (int i = 0; i < PPT_PIXELS; ++i) {
        float t = currentTemps[i];
        if (t < -40.0f) t = -40.0f;
        if (t > 300.0f) t = 300.0f;
        currentCenti[i] = (int16_t)roundf(t * 100.0f);
    }

    if (currentPPTMode == PPT_MODE_ARMED) {
        // Calculate max pixel temperature rate-of-rise across consecutive frames (for live telemetry display)
        float maxRise = 0.0f;
        if (hasPrevTemps) {
            for (int i = 0; i < PPT_PIXELS; ++i) {
                float diff = currentTemps[i] - prevTemps[i];
                if (diff > maxRise) {
                    maxRise = diff;
                }
            }
            currentDt = maxRise;
        }
        memcpy(prevTemps, currentTemps, sizeof(prevTemps));
        hasPrevTemps = true;

        // Store into the rolling 8-frame ring buffer (maintains pre-trigger baseline)
        memcpy(ringBuffer[ringHead], currentCenti, sizeof(currentCenti));
        ringHead = (ringHead + 1) % PPT_RING_FRAMES;
        if (ringCount < PPT_RING_FRAMES) ringCount++;

        // Trigger condition: strictly triggered on user button press (look-around preview remains stable)
        bool triggered = manualTriggerFlag;
        if (triggered) {
            manualTriggerFlag = false;
            Serial.printf("\n[PPT] *** RECORDING TRIGGERED VIA BUTTON PRESS! (dT = +%.2f C) -> LOCKING 8 BASELINE FRAMES ***\n", currentDt);

            // Lock the 8 pre-trigger frames in chronological order into transient_cube[0..7]
            if (ringCount >= PPT_RING_FRAMES) {
                for (int j = 0; j < PPT_RING_FRAMES; ++j) {
                    int slot = (ringHead + j) % PPT_RING_FRAMES;
                    memcpy(transient_cube[j], ringBuffer[slot], sizeof(ringBuffer[slot]));
                }
            } else {
                // If triggered early, duplicate earliest frame
                for (int j = 0; j < PPT_RING_FRAMES; ++j) {
                    int slot = (j < ringCount) ? j : (ringCount - 1);
                    if (slot < 0) slot = 0;
                    memcpy(transient_cube[j], ringBuffer[slot], sizeof(ringBuffer[slot]));
                }
            }

            acquiredFrames = PPT_RING_FRAMES; // 8 frames locked
            currentPPTMode = PPT_MODE_ACQUIRING;
            Serial.println("[PPT] >>> PPT STATE: ACQUIRING (Streaming 56 frames @ 16 Hz) <<<");
        }
    } else if (currentPPTMode == PPT_MODE_ACQUIRING) {
        // Stream subsequent frames 8..63 directly into the transient cube
        if (acquiredFrames < PPT_NUM_FRAMES) {
            memcpy(transient_cube[acquiredFrames], currentCenti, sizeof(currentCenti));
            acquiredFrames++;
        }

        if (acquiredFrames >= PPT_NUM_FRAMES) {
            Serial.println("[PPT] *** 64 FRAMES RECORDED SUCCESSFULLY! ***");
            Serial.println("[PPT] Transitioning to Phase Computation & Inspection...");

            // Compute phase map for default bin k = 4 (1.00 Hz)
            pptComputePhaseMap(currentBinK);

            currentPPTMode = PPT_MODE_INSPECT;
            Serial.printf("[PPT] >>> PPT STATE: INSPECT (Bin %d: %.2f Hz) <<<\n\n", 
                          currentBinK, pptGetBinFrequency(currentBinK));

            // Instantly wake up the display task on Core 1 to render the phase map
            if (displayTaskHandle != NULL) {
                xTaskNotifyGive(displayTaskHandle);
            }

            // If a BLE client is connected, perform upfront batch Goertzel extraction for all 16
            // harmonic frequencies, then dispatch dedicated wireless transmission immediately!
            if (isBLEConnected()) {
                if (s_activeSession != nullptr && pptExtractAllPhasegrams(s_activeSession)) {
                    Serial.println("[PPT] Pre-computed all 16 harmonic phasegrams in batch. Dispatching wireless BLE transfer...");
                    triggerPPTBLETransfer();
                }
            } else {
                Serial.println("[PPT] BLE disconnected; skipping wireless phasegram transfer.");
            }
        }
    }
}

// 1D Single-Bin Goertzel Phase Extraction Kernel for each of the 768 pixels
void pptComputePhaseMap(int binK) {
    if (transient_cube == nullptr) return;

    if (binK < PPT_MIN_BIN) binK = PPT_MIN_BIN;
    if (binK > PPT_MAX_BIN) binK = PPT_MAX_BIN;
    currentBinK = binK;

    // Interrogation angular frequency: omega = 2 * pi * k / N
    const float omega = (2.0f * 3.14159265358979323846f * (float)binK) / (float)PPT_NUM_FRAMES;
    const float coeff = 2.0f * cosf(omega);
    const float cosOmega = cosf(omega);
    const float sinOmega = sinf(omega);

    float minP = 1000.0f;
    float maxP = -1000.0f;

    // Loop across all 768 spatial pixels
    for (int p = 0; p < PPT_PIXELS; ++p) {
        // Average the 8 pre-trigger baseline frames (4 subpage 0 + 4 subpage 1) to cancel baseline noise and subpage bias
        float baseSum = 0.0f;
        for (int b = 0; b < PPT_RING_FRAMES; ++b) {
            baseSum += (float)transient_cube[b][p];
        }
        float baseline = baseSum * (1.0f / (float)PPT_RING_FRAMES);

        float s0 = 0.0f;
        float s1 = 0.0f;
        float s2 = 0.0f;

        // Run Goertzel recursion across the 64 temporal samples
        for (int n = 0; n < PPT_NUM_FRAMES; ++n) {
            float x = (float)transient_cube[n][p] - baseline;
            s0 = x + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        // Extract Real and Imaginary DFT components
        // Re = s1 - s2 * cos(omega)
        // Im = -s2 * sin(omega) (negative sign corresponds to standard DFT e^(-j*2*pi*k*n/N))
        float realPart = s1 - s2 * cosOmega;
        float imagPart = -s2 * sinOmega;

        // Extract phase angle: Phi = atan2(Im, Re) in radians [-pi, +pi]
        float phi = atan2f(imagPart, realPart);
        phaseGrid[p] = phi;

        if (phi < minP) minP = phi;
        if (phi > maxP) maxP = phi;
    }

    phaseMinVal = minP;
    phaseMaxVal = maxP;

    // Spot phase at optical center (average of 4 center pixels)
    float c0 = phaseGrid[11 * SENSOR_WIDTH + 15];
    float c1 = phaseGrid[11 * SENSOR_WIDTH + 16];
    float c2 = phaseGrid[12 * SENSOR_WIDTH + 15];
    float c3 = phaseGrid[12 * SENSOR_WIDTH + 16];
    phaseCenterVal = (c0 + c1 + c2 + c3) * 0.25f;

    // Dynamic contrast-enhanced normalization into 0 - 255 for spatial upscaling
    float pSpan = phaseMaxVal - phaseMinVal;
    float effMin = phaseMinVal;
    // Prevent noise explosion on completely uniform/flat phase fields
    if (pSpan < 0.25f) {
        float mid = 0.5f * (phaseMinVal + phaseMaxVal);
        pSpan = 0.50f;
        effMin = mid - 0.25f;
    }
    float invSpan = 255.0f / pSpan;

    for (int p = 0; p < PPT_PIXELS; ++p) {
        float norm = (phaseGrid[p] - effMin) * invSpan;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 255.0f) norm = 255.0f;
        normalizedPhaseGrid[p] = norm;
    }
}

void pptSetBin(int binK) {
    if (binK < PPT_MIN_BIN) binK = PPT_MIN_BIN;
    if (binK > PPT_MAX_BIN) binK = PPT_MAX_BIN;
    if (binK != currentBinK) {
        pptComputePhaseMap(binK);
    }
}

void pptStepBin(int delta) {
    int newBin = currentBinK + delta;
    if (newBin < PPT_MIN_BIN) newBin = PPT_MIN_BIN;
    if (newBin > PPT_MAX_BIN) newBin = PPT_MAX_BIN;
    if (newBin != currentBinK) {
        pptComputePhaseMap(newBin);
    }
}

int pptGetBin() {
    return currentBinK;
}

float pptGetBinFrequency(int binK) {
    if (binK < PPT_MIN_BIN) binK = PPT_MIN_BIN;
    if (binK > PPT_MAX_BIN) binK = PPT_MAX_BIN;
    return ((float)binK * PPT_SAMPLE_RATE_HZ) / (float)PPT_NUM_FRAMES;
}

const char* pptGetDepthDescription(int binK) {
    if (binK <= 2) {
        return "Deep";
    } else if (binK <= 5) {
        return "Mid-Deep";
    } else if (binK <= 10) {
        return "Medium";
    } else {
        return "Shallow";
    }
}

const float* pptGetPhaseGrid() {
    return phaseGrid;
}

const float* pptGetNormalizedPhaseGrid() {
    return normalizedPhaseGrid;
}

void pptGetPhaseStats(float& outMin, float& outMax, float& outCenter) {
    outMin = phaseMinVal;
    outMax = phaseMaxVal;
    outCenter = phaseCenterVal;
}

// -----------------------------------------------------------------------------
// Pulsed Phase Thermography (PPT) Batch Spectral Phasegram Extraction
// -----------------------------------------------------------------------------

static uint32_t updateCRC32(const uint8_t* data, size_t len, uint32_t crc) {
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
        }
    }
    return crc;
}

bool pptExtractAllPhasegrams(PPTSpectralSession* session) {
    if (transient_cube == nullptr || session == nullptr) return false;

    int64_t tStart = esp_timer_get_time();
    session->sessionTimestamp = millis();
    session->totalPhasegrams = PPT_MAX_BIN; // 16

    // Precalculate baseline across the 8 reference frames for all 768 pixels into PSRAM (zero stack usage)
    for (int p = 0; p < PPT_PIXELS; ++p) {
        float sum = 0.0f;
        for (int b = 0; b < PPT_RING_FRAMES; ++b) {
            sum += (float)transient_cube[b][p];
        }
        session->baselines[p] = sum * (1.0f / (float)PPT_RING_FRAMES);
    }

    // Batch extraction across all 16 harmonic interrogation frequencies (0.25 Hz - 4.00 Hz)
    for (int k = PPT_MIN_BIN; k <= PPT_MAX_BIN; ++k) {
        int idx = k - 1;
        PPTPhasegram& pg = session->phasegrams[idx];
        pg.harmonicBin = k;
        pg.frequencyHz = pptGetBinFrequency(k);
        pg.diffusionDepth = pptGetDepthDescription(k);

        const float omega = (2.0f * 3.14159265358979323846f * (float)k) / (float)PPT_NUM_FRAMES;
        const float coeff = 2.0f * cosf(omega);
        const float cosOmega = cosf(omega);
        const float sinOmega = sinf(omega);

        float minP = 1000.0f;
        float maxP = -1000.0f;

        for (int p = 0; p < PPT_PIXELS; ++p) {
            float base = session->baselines[p];
            float s0 = 0.0f;
            float s1 = 0.0f;
            float s2 = 0.0f;

            for (int n = 0; n < PPT_NUM_FRAMES; ++n) {
                float x = (float)transient_cube[n][p] - base;
                s0 = x + coeff * s1 - s2;
                s2 = s1;
                s1 = s0;
            }

            float realPart = s1 - s2 * cosOmega;
            float imagPart = -s2 * sinOmega;
            float phi = atan2f(imagPart, realPart);
            pg.rawPhaseGrid[p] = phi;

            if (phi < minP) minP = phi;
            if (phi > maxP) maxP = phi;
        }

        pg.minPhaseRad = minP;
        pg.maxPhaseRad = maxP;

        // Spot phase at optical center (average of 4 center pixels)
        float c0 = pg.rawPhaseGrid[11 * SENSOR_WIDTH + 15];
        float c1 = pg.rawPhaseGrid[11 * SENSOR_WIDTH + 16];
        float c2 = pg.rawPhaseGrid[12 * SENSOR_WIDTH + 15];
        float c3 = pg.rawPhaseGrid[12 * SENSOR_WIDTH + 16];
        pg.centerPhaseRad = (c0 + c1 + c2 + c3) * 0.25f;

        // Dynamic contrast-enhanced normalization into [0, 255]
        float pSpan = maxP - minP;
        float effMin = minP;
        if (pSpan < 0.25f) {
            float mid = 0.5f * (minP + maxP);
            pSpan = 0.50f;
            effMin = mid - 0.25f;
        }
        float invSpan = 255.0f / pSpan;

        for (int p = 0; p < PPT_PIXELS; ++p) {
            float norm = (pg.rawPhaseGrid[p] - effMin) * invSpan;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 255.0f) norm = 255.0f;
            pg.normPhaseGrid[p] = (uint8_t)norm;
        }

        // IEEE 802.3 CRC32 checksum over the 768-byte normalized phasegram image
        uint32_t runningCrc = 0xFFFFFFFF;
        runningCrc = updateCRC32(pg.normPhaseGrid, PPT_PIXELS, runningCrc);
        pg.crc32 = ~runningCrc;
    }

    int64_t tElapsedUs = esp_timer_get_time() - tStart;
    Serial.printf("[PPT] Batch Goertzel extracted 16 harmonic phasegrams in %lld us (%.2f ms)\n",
                  tElapsedUs, tElapsedUs / 1000.0f);
    return true;
}

PPTSpectralSession* pptGetActiveSession() {
    return s_activeSession;
}

