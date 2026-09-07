#include "sensor.hpp"
#include "diagnostics.hpp"
#include <esp_timer.h>

Adafruit_MLX90640 mlx;

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

    // Switch to 800kHz for high-speed streaming
    Wire.setClock(I2C_FREQ_RUN);

    mlx.setMode(MLX90640_CHESS);
    mlx.setResolution(MLX90640_ADC_18BIT);
    mlx.setRefreshRate(THERMAL_REFRESH_RATE);

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

// Technique A: De-dither the checkerboard subpage mismatch artifact from MLX90640 Chess mode.
// Because the two chess subpages are read at different time instants, slight Vdd ripple
// or ADC reference drift on breadboards causes a systematic offset between Subpage 0 and Subpage 1.
// Every orthogonal neighbor of Subpage 0 belongs to Subpage 1, so the Laplacian average
// across the frame reveals the exact inter-subpage offset and eliminates it in ~10 microseconds.
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
                Wire.setClock(I2C_FREQ_RUN);
                mlx.setMode(MLX90640_CHESS);
                mlx.setResolution(MLX90640_ADC_18BIT);
                mlx.setRefreshRate(THERMAL_REFRESH_RATE);

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

            // Technique A: Remove high-frequency chess subpage mismatch / criss-cross pattern
            balanceChessSubpages(localFrame.temperatures);

            float tMin = 1000.0f;
            float tMax = -1000.0f;

            // Compute min and max across all 768 pixels
            for (size_t i = 0; i < SENSOR_PIXELS; ++i) {
                float val = localFrame.temperatures[i];
                if (val > -50.0f && val < 400.0f) {
                    if (val < tMin) tMin = val;
                    if (val > tMax) tMax = val;
                }
            }

            // Calculate center spot temperature (average of 4 center pixels)
            float c0 = localFrame.temperatures[11 * SENSOR_WIDTH + 15];
            float c1 = localFrame.temperatures[11 * SENSOR_WIDTH + 16];
            float c2 = localFrame.temperatures[12 * SENSOR_WIDTH + 15];
            float c3 = localFrame.temperatures[12 * SENSOR_WIDTH + 16];
            localFrame.centerTemp = (c0 + c1 + c2 + c3) * 0.25f;

            localFrame.minTemp = tMin;
            localFrame.maxTemp = tMax;
            localFrame.frameIndex++;

            postThermalFrame(localFrame);

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
