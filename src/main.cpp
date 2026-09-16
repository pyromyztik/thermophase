#include <Arduino.h>
#include "config.hpp"
#include "sensor.hpp"
#include "display.hpp"
#include "rgb-led.hpp"
#include "diagnostics.hpp"
#include "wheel.hpp"
#include "ui.hpp"
#include "power.hpp"
#include "storage.hpp"
#include "serial.hpp"

// Global task handles
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t diagTaskHandle = NULL;
TaskHandle_t ledTaskHandle = NULL;

// Global hardware health statuses
volatile DeviceStatus globalDeviceStatus = STATUS_INITIALIZING;
volatile SensorStatus globalSensorStatus = SENSOR_DISCONNECTED;
volatile uint32_t sensorReconnectAttempts = 0;

void setup() {
    // 0. Ensure TFT backlight is immediately OFF to eliminate any white flash
    pinMode(TFT_LED, OUTPUT);
    digitalWrite(TFT_LED, LOW);

    // Initialize power state and check if awakened from Deep Sleep
    powerWakeInit();
    initBatteryMonitor();

    Serial.begin(115200);
    while (!Serial && millis() < 800) 
        { /* Wait briefly for native USB CDC attachment without stalling cold boot */ }
    Serial.setTimeout(50);

    Serial.println("\n========================================================");
    Serial.println("  ESP32-S3 Real-Time Bilinear Thermal Camera System");
    Serial.println("========================================================");

    // Load user settings from ESP32 NVS (Preferences) before initializing Display & UI
    loadSettings();

    // 1. Initialize ILI9341 Display & Bilinear tables FIRST and render Boot Screen
    // (TFT_LED is kept LOW until the boot screen is drawn in canvas and pushed to LCD memory)
    if (!initDisplay()) {
        Serial.println("[MAIN] Display initialization failed! Check wiring & SPI lines.");
    }
    showBootScreen("Loading UI & Controls...", 25);

    // 2. Initialize On-board RGB LED
    initRGB();
    // 3. Spawn RGB LED task immediately so LED feedback is live during boot
    xTaskCreatePinnedToCore(
        ledAnimationTask,
        "LED_Task",
        LED_TASK_STACK,
        NULL,
        LED_TASK_PRIO,
        &ledTaskHandle,
        LED_TASK_CORE
    );

    // 4. Initialize HW-040 Rotary Wheel (GPIO 4, 5, 6)
    if (!initWheel()) {
        Serial.println("[MAIN] HW-040 Rotary Wheel initialization failed!");
    }
    // 5. Spawn HW-040 Wheel polling task on Core 0
    xTaskCreatePinnedToCore(
        wheelTask,
        "Wheel_Task",
        WHEEL_TASK_STACK,
        NULL,
        WHEEL_TASK_PRIO,
        &wheelTaskHandle,
        WHEEL_TASK_CORE
    );

    showBootScreen("Connecting MLX90640...", 55);
    // 7. Initialize MLX90640 Thermal Sensor (includes 2-subpage warmup)
    if (!initThermalSensor()) {
        Serial.println("[MAIN] Thermal Sensor not detected. Auto-reconnect will poll in background.");
        showBootScreen("Sensor Disconnected - Retrying...", 75);
    } else {
        showBootScreen("Calibrating Thermal Feed...", 85);
    }

    // 8. Create Thermal Sensor acquisition task on Core 0
    xTaskCreatePinnedToCore(
        thermalSensorTask,
        "Sensor_Task",
        SENSOR_TASK_STACK,
        NULL,
        SENSOR_TASK_PRIO,
        &sensorTaskHandle,
        SENSOR_TASK_CORE
    );

    showBootScreen("System Ready!", 100);
    delay(350); // Clean, polished hold so user sees full boot completion

    // 9. Create Display & Bilinear scaling task on Core 1
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
    // Process keyboard & serial commands for encoder emulation
    processSerialInput();

    // Continuous status and error reporting every 2500 ms
    unsigned long now = millis();
    if (now - lastTelemetryTime >= 2500) {
        lastTelemetryTime = now;

        bool displayErr = (globalDeviceStatus != STATUS_OK);
        bool sensorErr = (globalSensorStatus != SENSOR_OK);

        if (displayErr || sensorErr) {
            // PROMINENT CONTINUOUS HARDWARE ALERT BANNER
            Serial.println("\n****************************************************************");
            Serial.println("                  >>> HARDWARE ALERT ACTIVE <<<                 ");
            Serial.println("****************************************************************");

            if (displayErr && sensorErr) {
                Serial.println(" [!] FAULT: BOTH DISPLAY AND SENSOR HAVE CONNECTION ISSUES!");
                Serial.println(" [*] ON-BOARD RGB LED: Alternating RED & YELLOW flashing");
            } else if (displayErr) {
                Serial.println(" [!] FAULT: TFT DISPLAY CONNECTION ISSUE DETECTED");
                Serial.println(" [*] ON-BOARD RGB LED: Blinking RED");
            } else {
                Serial.println(" [!] FAULT: MLX90640 THERMAL SENSOR DISCONNECTED / NO SIGNAL");
                Serial.println(" [*] ON-BOARD RGB LED: Blinking BRIGHT YELLOW");
            }

            if (displayErr) {
                Serial.println("\n --- DISPLAY (ILI9341) TROUBLESHOOTING ---");
                Serial.println("  1. If screen is BLANK WHITE: Check SPI signals & power:");
                Serial.printf("     - CS: GPIO %d | DC: GPIO %d | RST: GPIO %d\n", TFT_CS, TFT_DC, TFT_RST);
                Serial.printf("     - MOSI: GPIO %d (SDI) | SCK: GPIO %d | LED: GPIO %d / 3.3V\n", TFT_MOSI, TFT_SCK, TFT_LED);
                Serial.printf("     - VCC: 5V/3.3V | GND: Common Ground\n");
                Serial.println("  2. If screen IS SHOWING the image normally, but LED is red:");
                Serial.printf("     - MISO (GPIO %d) is not connected to SDO on display.\n", TFT_MISO);
                Serial.println("       (Set CHECK_TFT_MISO false in config.hpp to disable MISO check)");
            }

            if (sensorErr) {
                Serial.println("\n --- THERMAL SENSOR (MLX90640) TROUBLESHOOTING ---");
                Serial.printf("  Status: No response at I2C address 0x%02X (Reconnect attempts: %u)\n",
                              MLX90640_I2CADDR_DEFAULT, sensorReconnectAttempts);
                Serial.println("  Check jumper cables:");
                Serial.printf("     - SDA: GPIO %d\n", PIN_I2C_SDA);
                Serial.printf("     - SCL: GPIO %d\n", PIN_I2C_SCL);
                Serial.println("     - VIN: 3.3V (CAUTION: Do NOT connect to 5V!)");
                Serial.println("     - GND: Common Ground with ESP32");
                Serial.println("  Auto-reconnecting every 1 second in background...");
            }

            Serial.printf("\n Uptime: %lu s | Free Heap: %u bytes\n", now / 1000, ESP.getFreeHeap());
            Serial.println("****************************************************************\n");
        } else {
            // All systems healthy: periodic telemetry with compute headroom metrics
            Serial.printf("[HEALTHY] Live Feed: %.1f FPS | Spot: %.1f C | Range: [%.1f - %.1f C] | Uptime: %lu s\n",
                          displayFps, displayCenterTemp, displayMinTemp, displayMaxTemp, now / 1000);
            Serial.printf(" [COMPUTE] Core 1 (Display/Upscale): %4.1f%% (Scale: %4.1f ms, UI+Flush: %4.1f ms) | Headroom: %4.1f ms (%4.1f%%)\n",
                          globalComputeStats.core1LoadPct, globalComputeStats.scaleTimeMs, globalComputeStats.flushTimeMs,
                          globalComputeStats.core1HeadroomMs, globalComputeStats.core1HeadroomPct);
            Serial.printf(" [COMPUTE] Core 0 (Sensor/I2C):     %4.1f%% (I2C: %4.1f ms) | Free DRAM: %u KB | Free PSRAM: %u KB\n\n",
                          globalComputeStats.core0LoadPct, globalComputeStats.sensorReadMs,
                          ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
        }
    }

    delay(10); // Small yield
}

