#include "thermal_sensor.hpp"

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

bool initThermalSensor() {
    if (frameMutex == NULL) {
        frameMutex = xSemaphoreCreateMutex();
    }
    Serial.println("[MLX90640] Initializing I2C bus...");
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    // EEPROM must be read at <= 400kHz during begin()
    Wire.setClock(I2C_FREQ_INIT);

    if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
        Serial.println("[MLX90640] ERROR: Sensor not detected! Check SDA/SCL wiring and pull-ups.");
        return false;
    }
    Serial.println("[MLX90640] Sensor detected successfully!");

    // Switch to 800kHz for streaming
    Wire.setClock(I2C_FREQ_RUN);

    mlx.setMode(MLX90640_CHESS);
    mlx.setResolution(MLX90640_ADC_18BIT);
    mlx.setRefreshRate(THERMAL_REFRESH_RATE);

    Serial.printf("[MLX90640] Sensor ready. Refresh Rate setting: %d, I2C Clock: %lu Hz\n", (int)THERMAL_REFRESH_RATE, Wire.getClock());
    return true;
}

void thermalSensorTask(void *pvParameters) {
    Serial.println("[TASK] Thermal Sensor Task started on Core " + String(xPortGetCoreID()));

    ThermalFrame localFrame;
    localFrame.frameIndex = 0;

    while (true) {
        // mlx.getFrame() blocks until a subpage is ready from sensor hardware
        int status = mlx.getFrame(localFrame.temperatures);

        if (status == 0) {
            float tMin = 1000.0f;
            float tMax = -1000.0f;

            // Compute min and max across all 768 pixels
            for (size_t i = 0; i < SENSOR_PIXELS; ++i) {
                float val = localFrame.temperatures[i];
                // Sanity check filtering against sensor glitches
                if (val > -50.0f && val < 400.0f) {
                    if (val < tMin) tMin = val;
                    if (val > tMax) tMax = val;
                }
            }

            // Calculate center spot temperature (average of 4 center pixels 11..12, 15..16)
            float c0 = localFrame.temperatures[11 * SENSOR_WIDTH + 15];
            float c1 = localFrame.temperatures[11 * SENSOR_WIDTH + 16];
            float c2 = localFrame.temperatures[12 * SENSOR_WIDTH + 15];
            float c3 = localFrame.temperatures[12 * SENSOR_WIDTH + 16];
            localFrame.centerTemp = (c0 + c1 + c2 + c3) * 0.25f;

            localFrame.minTemp = tMin;
            localFrame.maxTemp = tMax;
            localFrame.frameIndex++;

            // Post frame into thread-safe buffer
            postThermalFrame(localFrame);

            // Notify display task that a fresh frame is ready to render
            if (displayTaskHandle != NULL) {
                xTaskNotifyGive(displayTaskHandle);
            }
        } else {
            // Error fetching frame, yield briefly
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}

