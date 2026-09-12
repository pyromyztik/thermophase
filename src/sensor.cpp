#include "sensor.hpp"
#include "diagnostics.hpp"
#include "ppt.hpp"
#include <esp_timer.h>
#include <algorithm>
#include <cmath>

Adafruit_MLX90640 mlx;

// Runtime configurable I2C bus speed
static volatile uint32_t currentSensorI2CSpeed = I2C_FREQ_RUN;

void setSensorI2CSpeed(uint32_t speedHz) {
    if (speedHz != currentSensorI2CSpeed) {
        currentSensorI2CSpeed = speedHz;
        if (globalSensorStatus == SENSOR_OK) {
            Wire.setClock(speedHz);
            // 400kHz I2C cannot transfer 32 subpages/sec (takes ~42ms > 31.25ms period).
            // Coordinate sensor refresh rate so 400kHz never overflows the bus.
            if (speedHz <= I2C_SPEED_400K) {
                mlx.setRefreshRate(MLX90640_16_HZ);
            } else {
                mlx.setRefreshRate(THERMAL_REFRESH_RATE);
            }
            Serial.printf("[MLX90640] I2C Clock updated dynamically to %lu Hz\n", speedHz);
        }
    }
}

uint32_t getSensorI2CSpeed() {
    return currentSensorI2CSpeed;
}

// Shared thread-safe frame double-buffer
static ThermalFrame sharedFrame;
static SemaphoreHandle_t frameMutex = NULL;
static bool hasNewFrame = false;

void postThermalFrame(const ThermalFrame& frame) {
    if (frameMutex != NULL && xSemaphoreTake(frameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        sharedFrame = frame;
        hasNewFrame = true;
        xSemaphoreGive(frameMutex);
    }
}

bool getLatestThermalFrame(ThermalFrame& outFrame) {
    bool updated = false;
    if (frameMutex != NULL && xSemaphoreTake(frameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (hasNewFrame) {
            outFrame = sharedFrame;
            hasNewFrame = false;
            updated = true;
        }
        xSemaphoreGive(frameMutex);
    }
    return updated;
}

static void scanI2CBus() {
    Serial.println("\n--------------------------------------------------");
    Serial.printf("  I2C Bus Scan (SDA: GPIO %d, SCL: GPIO %d)   \n", PIN_I2C_SDA, PIN_I2C_SCL);
    Serial.println("--------------------------------------------------");
    int devicesFound = 0;
    for (uint8_t addr = 1; addr < 127; ++addr) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            Serial.printf("  [+] Device found at 0x%02X", addr);
            if (addr == MLX90640_I2CADDR_DEFAULT) {
                Serial.print("  <-- MLX90640 Thermal Camera");
            }
            Serial.println();
            devicesFound++;
        }
    }
    if (devicesFound == 0) {
        Serial.println("  [-] NO I2C DEVICES FOUND on the bus!");
        Serial.println("      Possible causes: loose SDA/SCL, missing 3.3V power, or missing GND.");
    }
    Serial.println("--------------------------------------------------\n");
}

bool initThermalSensor() {
    if (frameMutex == NULL) {
        frameMutex = xSemaphoreCreateMutex();
    }

    Serial.println("[MLX90640] Initializing I2C bus...");
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(I2C_FREQ_INIT);

    // Run active bus scan to show connected I2C devices on Serial Monitor
    scanI2CBus();

    if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
        globalSensorStatus = SENSOR_DISCONNECTED;
        Serial.println("\n========================================================");
        Serial.println(" [ERROR] MLX90640 THERMAL SENSOR NOT DETECTED!");
        Serial.printf(" No response at I2C address 0x%02X.\n", MLX90640_I2CADDR_DEFAULT);
        Serial.println(" Hardware Checklist for MLX90640:");
        Serial.printf("   - SDA -> GPIO %d\n", PIN_I2C_SDA);
        Serial.printf("   - SCL -> GPIO %d\n", PIN_I2C_SCL);
        Serial.println("   - VIN -> 3.3V (Do NOT connect to 5V!)");
        Serial.println("   - GND -> Common GND with ESP32");
        Serial.println("========================================================\n");
        return false;
    }

    Serial.println("[MLX90640] Sensor detected successfully!");

    // Switch to configured speed for streaming (400kHz clean or 800kHz fast)
    Wire.setClock(currentSensorI2CSpeed);

    mlx.setMode(MLX90640_CHESS);
    mlx.setResolution(MLX90640_ADC_18BIT);
    if (currentSensorI2CSpeed <= I2C_SPEED_400K && THERMAL_REFRESH_RATE > MLX90640_16_HZ) {
        mlx.setRefreshRate(MLX90640_16_HZ);
    } else {
        mlx.setRefreshRate(THERMAL_REFRESH_RATE);
    }

    // Warm-up sensor: read 2 initial subpages so both chess subpages are primed
    float warmupBuf[SENSOR_PIXELS];
    for (size_t i = 0; i < SENSOR_PIXELS; ++i) warmupBuf[i] = 25.0f;
    mlx.getFrame(warmupBuf);
    mlx.getFrame(warmupBuf);

    globalSensorStatus = SENSOR_OK;
    Serial.printf("[MLX90640] Sensor ready. Refresh setting: %d, I2C Clock: %lu Hz\n",
                  (int)THERMAL_REFRESH_RATE, Wire.getClock());
    return true;
}

// Technique A: Subpage Chess Offset Balancer (De-dithering)
// Because the criss-cross pattern is caused by a mean offset between Subpage 0 and Subpage 1,
// we compute the average difference Δ between Subpage 0 pixels and their adjacent Subpage 1 neighbors.
// Subtract Δ/2 from Subpage 0 and add Δ/2 to Subpage 1.
// Result: Cancels the checkerboard dither completely without blurring actual thermal edges.
static void balanceChessSubpages(float* temps) {
    float sumDiff = 0.0f;
    int count = 0;

    for (int r = 1; r < SENSOR_HEIGHT - 1; ++r) {
        for (int c = 1; c < SENSOR_WIDTH - 1; ++c) {
            if (((r + c) & 1) == 0) {
                float neighborAvg = 0.25f * (
                    temps[(r - 1) * SENSOR_WIDTH + c] +
                    temps[(r + 1) * SENSOR_WIDTH + c] +
                    temps[r * SENSOR_WIDTH + (c - 1)] +
                    temps[r * SENSOR_WIDTH + (c + 1)]
                );
                sumDiff += (temps[r * SENSOR_WIDTH + c] - neighborAvg);
                count++;
            }
        }
    }

    if (count > 0) {
        float halfOffset = (sumDiff / (float)count) * 0.5f;

        // Safety clamp: normal subpage drift is 0.1C to 1.0C; avoid over-correcting sharp scene steps
        if (halfOffset > 2.0f) halfOffset = 2.0f;
        if (halfOffset < -2.0f) halfOffset = -2.0f;

        for (int r = 0; r < SENSOR_HEIGHT; ++r) {
            for (int c = 0; c < SENSOR_WIDTH; ++c) {
                int idx = r * SENSOR_WIDTH + c;
                if (((r + c) & 1) == 0) {
                    temps[idx] -= halfOffset;
                } else {
                    temps[idx] += halfOffset;
                }
            }
        }
    }
}

// Detect and repair corrupted pixels, dead sensor elements, and NaNs/Infs.
void cleanSensorOutliers(float* temps) {
    for (int r = 0; r < SENSOR_HEIGHT; ++r) {
        for (int c = 0; c < SENSOR_WIDTH; ++c) {
            int idx = r * SENSOR_WIDTH + c;
            float val = temps[idx];
            bool isBad = !std::isfinite(val) || val < -40.0f || val > 350.0f;

            if (isBad) {
                // Heal bad pixel by replacing with surrounding valid neighbors median
                float nVals[8];
                int nCount = 0;
                for (int dr = -1; dr <= 1; ++dr) {
                    int nr = r + dr;
                    if (nr < 0 || nr >= SENSOR_HEIGHT) continue;
                    for (int dc = -1; dc <= 1; ++dc) {
                        if (dr == 0 && dc == 0) continue;
                        int nc = c + dc;
                        if (nc < 0 || nc >= SENSOR_WIDTH) continue;
                        float nVal = temps[nr * SENSOR_WIDTH + nc];
                        if (std::isfinite(nVal) && nVal > -40.0f && nVal < 350.0f) {
                            nVals[nCount++] = nVal;
                        }
                    }
                }
                if (nCount > 0) {
                    std::nth_element(nVals, nVals + nCount / 2, nVals + nCount);
                    temps[idx] = nVals[nCount / 2];
                } else {
                    temps[idx] = 25.0f;
                }
            }
        }
    }
}

// 3x3 edge-preserving median filter on 32x24 raw thermal grid
// Eliminates high-frequency noise and residual chess pattern while preserving true physical boundaries
void filterSnapshotFrame(const float* src, float* dst) {
    float window[9];
    for (int r = 0; r < SENSOR_HEIGHT; ++r) {
        for (int c = 0; c < SENSOR_WIDTH; ++c) {
            int count = 0;
            for (int dr = -1; dr <= 1; ++dr) {
                int nr = r + dr;
                if (nr < 0 || nr >= SENSOR_HEIGHT) continue;
                for (int dc = -1; dc <= 1; ++dc) {
                    int nc = c + dc;
                    if (nc < 0 || nc >= SENSOR_WIDTH) continue;
                    window[count++] = src[nr * SENSOR_WIDTH + nc];
                }
            }
            std::nth_element(window, window + count / 2, window + count);
            dst[r * SENSOR_WIDTH + c] = window[count / 2];
        }
    }
}

void thermalSensorTask(void *pvParameters) {
    Serial.println("[TASK] Thermal Sensor Task started on Core " + String(xPortGetCoreID()));

    ThermalFrame localFrame = {};
    for (size_t i = 0; i < SENSOR_PIXELS; ++i) {
        localFrame.temperatures[i] = 25.0f;
    }
    localFrame.frameIndex = 0;
    int consecutiveErrors = 0;

    while (true) {
        // Handle disconnected state: attempt periodic auto-reconnection
        if (globalSensorStatus != SENSOR_OK) {
            sensorReconnectAttempts++;
            vTaskDelay(pdMS_TO_TICKS(1000));

            Serial.printf("[MLX90640] Reconnect attempt #%u...\n", sensorReconnectAttempts);

            // Re-initialize Wire bus to clear any bus lockup
            Wire.end();
            vTaskDelay(pdMS_TO_TICKS(20));
            Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
            Wire.setClock(I2C_FREQ_INIT);

            if (mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
                Wire.setClock(currentSensorI2CSpeed);
                mlx.setMode(MLX90640_CHESS);
                mlx.setResolution(MLX90640_ADC_18BIT);
                if (currentSensorI2CSpeed <= I2C_SPEED_400K && THERMAL_REFRESH_RATE > MLX90640_16_HZ) {
                    mlx.setRefreshRate(MLX90640_16_HZ);
                } else {
                    mlx.setRefreshRate(THERMAL_REFRESH_RATE);
                }

                globalSensorStatus = SENSOR_OK;
                consecutiveErrors = 0;
                sensorReconnectAttempts = 0;
                Serial.println("[MLX90640] SUCCESS: Thermal sensor reconnected!");
            }
            continue;
        }

        static int64_t lastSensorStartUs = 0;
        int64_t sensorStartUs = esp_timer_get_time();
        int64_t sensorIntervalUs = (lastSensorStartUs > 0) ? (sensorStartUs - lastSensorStartUs) : 62500;
        lastSensorStartUs = sensorStartUs;

        // mlx.getFrame() reads frame subpages over I2C
        int status = mlx.getFrame(localFrame.temperatures);

        if (status == 0) {
            consecutiveErrors = 0;

            // 0. Detect and repair bad/corrupted pixels and impulse spikes
            cleanSensorOutliers(localFrame.temperatures);

            // 1. Balance subpages (inter-subpage spatial chess offset correction)
            balanceChessSubpages(localFrame.temperatures);

            // 2. Inter-Subpage Frame Mean Balancer (Eliminates whole-frame 8 Hz thermal bouncing)
            static float evenSubpageMean = 0.0f;
            static float oddSubpageMean = 0.0f;
            static bool subpageMeansInitialized = false;
            static uint32_t subpageCounter = 0;

            float frameSum = 0.0f;
            for (size_t i = 0; i < SENSOR_PIXELS; ++i) {
                frameSum += localFrame.temperatures[i];
            }
            float currentFrameMean = frameSum / (float)SENSOR_PIXELS;

            bool isSubpage0 = ((subpageCounter & 1) == 0);
            subpageCounter++;

            if (!subpageMeansInitialized) {
                evenSubpageMean = currentFrameMean;
                oddSubpageMean = currentFrameMean;
                subpageMeansInitialized = true;
            } else {
                if (isSubpage0) {
                    evenSubpageMean += 0.15f * (currentFrameMean - evenSubpageMean);
                } else {
                    oddSubpageMean += 0.15f * (currentFrameMean - oddSubpageMean);
                }
            }

            float subpageBounceDelta = evenSubpageMean - oddSubpageMean;
            if (subpageBounceDelta > 1.0f) subpageBounceDelta = 1.0f;
            if (subpageBounceDelta < -1.0f) subpageBounceDelta = -1.0f;

            float frameCorrection = isSubpage0 ? (-0.5f * subpageBounceDelta) : (0.5f * subpageBounceDelta);
            for (size_t i = 0; i < SENSOR_PIXELS; ++i) {
                localFrame.temperatures[i] += frameCorrection;
            }

            // 2. Compute absolute min/max and robust percentiles (2nd & 98th) to discard outliers/bad pixels
            float tMin = 1000.0f;
            float tMax = -1000.0f;
            static float sortedTemps[SENSOR_PIXELS];
            int validCount = 0;

            for (size_t i = 0; i < SENSOR_PIXELS; ++i) {
                float val = localFrame.temperatures[i];
                if (std::isfinite(val) && val > -50.0f && val < 400.0f) {
                    if (val < tMin) tMin = val;
                    if (val > tMax) tMax = val;
                    sortedTemps[validCount++] = val;
                }
            }

            if (validCount > 64) {
                // Discard lowest 2% (index ~15) and highest 2% (index ~validCount - 16)
                int pLowIdx = validCount * 2 / 100;
                int pHighIdx = validCount - 1 - pLowIdx;
                if (pLowIdx < 0) pLowIdx = 0;
                if (pHighIdx >= validCount) pHighIdx = validCount - 1;

                std::nth_element(sortedTemps, sortedTemps + pLowIdx, sortedTemps + validCount);
                localFrame.robustMin = sortedTemps[pLowIdx];

                std::nth_element(sortedTemps + pLowIdx + 1, sortedTemps + pHighIdx, sortedTemps + validCount);
                localFrame.robustMax = sortedTemps[pHighIdx];
            } else {
                localFrame.robustMin = (tMin < 500.0f) ? tMin : 20.0f;
                localFrame.robustMax = (tMax > -500.0f) ? tMax : 30.0f;
            }

            // 4. Calculate center spot temperature with temporal smoothing (alpha = 0.25)
            float c0 = localFrame.temperatures[11 * SENSOR_WIDTH + 15];
            float c1 = localFrame.temperatures[11 * SENSOR_WIDTH + 16];
            float c2 = localFrame.temperatures[12 * SENSOR_WIDTH + 15];
            float c3 = localFrame.temperatures[12 * SENSOR_WIDTH + 16];
            float rawCenter = (c0 + c1 + c2 + c3) * 0.25f;

            static float smoothedCenter = 25.0f;
            static bool centerInitialized = false;
            if (!centerInitialized) {
                smoothedCenter = rawCenter;
                centerInitialized = true;
            } else {
                if (std::abs(rawCenter - smoothedCenter) > 0.05f) {
                    smoothedCenter += 0.25f * (rawCenter - smoothedCenter);
                }
            }
            localFrame.centerTemp = smoothedCenter;

            localFrame.minTemp = tMin;
            localFrame.maxTemp = tMax;
            localFrame.frameIndex++;

            postThermalFrame(localFrame);

            // Feed into PPT engine if armed or acquiring
            if (isPPTArmed() || isPPTAcquiring()) {
                pptProcessSensorFrame(localFrame.temperatures);
            }

            // Notify display task that a fresh frame is ready
            if (displayTaskHandle != NULL) {
                xTaskNotifyGive(displayTaskHandle);
            }

            int64_t sensorEndUs = esp_timer_get_time();
            logSensorProfile(sensorEndUs - sensorStartUs, sensorIntervalUs);
        } else {
            consecutiveErrors++;
            Serial.printf("[MLX90640] Warning: getFrame returned error code %d (consecutive: %d)\n",
                          status, consecutiveErrors);

            if (consecutiveErrors >= 5) {
                Serial.println("\n[MLX90640] ERROR: Sensor signal lost! Wire disconnected or bus locked.");
                globalSensorStatus = SENSOR_DISCONNECTED;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
