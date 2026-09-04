#include <Arduino.h>
#include "config.hpp"
#include "thermal_types.hpp"
#include "thermal_sensor.hpp"
#include "display_task.hpp"
#include "rgb_led.hpp"
#include "diagnostics.hpp"

// Global task handles
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t diagTaskHandle = NULL;
TaskHandle_t ledTaskHandle = NULL;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) 
        { /* Wait briefly for native USB CDC attachment */ }
    Serial.setTimeout(50);

    Serial.println("\n========================================================");
    Serial.println("  ESP32-S3 Real-Time Bilinear Thermal Camera System");
    Serial.println("========================================================");

    // 1. Initialize On-board RGB LED
    initRGB();

    // 2. Initialize ILI9341 Display & Bilinear tables
    if (!initDisplay()) {
        Serial.println("[MAIN] Display initialization failed! Halting.");
        while (true) { delay(1000); }
    }

    // 3. Initialize MLX90640 Thermal Sensor
    if (!initThermalSensor()) {
        Serial.println("[MAIN] Thermal Sensor initialization failed! Check wiring.");
        // Continue to allow diagnostics & LED
    }
    
    // 4. Create RGB LED animation task on Core 0
    xTaskCreatePinnedToCore(
        ledAnimationTask,
        "LED_Task",
        LED_TASK_STACK,
        NULL,
        LED_TASK_PRIO,
        &ledTaskHandle,
        LED_TASK_CORE
    );
    // 5. Create Thermal Sensor acquisition task on Core 0
    xTaskCreatePinnedToCore(
        thermalSensorTask,
        "Sensor_Task",
        SENSOR_TASK_STACK,
        NULL,
        SENSOR_TASK_PRIO,
        &sensorTaskHandle,
        SENSOR_TASK_CORE
    );
    // 6. Create Display & Bilinear scaling task on Core 1
    xTaskCreatePinnedToCore(
        displayTask,
        "Display_Task",
        DISPLAY_TASK_STACK,
        NULL,
        DISPLAY_TASK_PRIO,
        &displayTaskHandle,
        DISPLAY_TASK_CORE
    );
    Serial.println("\n[SYSTEM] All parallel tasks spawned successfully!");
    Serial.println("[SYSTEM] Type 'diagnostics' in Serial Monitor to run hardware diagnostics.\n");
}

static unsigned long lastTelemetryTime = 0;

void loop() {
    // Check for user command over Serial Monitor
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();

        if (input.equalsIgnoreCase("diagnostics")) {
            if (diagTaskHandle == NULL) {
                Serial.println("\n[CMD] Spawning System Diagnostics Task...");
                xTaskCreatePinnedToCore(
                    diagnosticsMessageTask,
                    "Diag_Task",
                    DIAG_TASK_STACK,
                    NULL,
                    DIAG_TASK_PRIO,
                    &diagTaskHandle,
                    DIAG_TASK_CORE
                );
            } else {
                Serial.println("\n[WARN] Diagnostics task is already running.");
            }
        }
    }
    // Periodic telemetry output every 2 seconds
    unsigned long now = millis();
    if (now - lastTelemetryTime >= 2000) {
        lastTelemetryTime = now;
        Serial.printf("[TELEMETRY] Uptime: %lu ms | Free Heap: %u bytes | Min Free: %u bytes\n",
                      now, ESP.getFreeHeap(), ESP.getMinFreeHeap());
    }
    delay(10); // Small yield
}

