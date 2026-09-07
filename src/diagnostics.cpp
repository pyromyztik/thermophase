#include "diagnostics.hpp"
#include "display.hpp"
#include "wheel.hpp"
#include "rgb-led.hpp"

// Global compute statistics instance
volatile ComputeTelemetry globalComputeStats = {
    .core1LoadPct = 0.0f,
    .scaleTimeMs = 0.0f,
    .flushTimeMs = 0.0f,
    .frameTotalMs = 0.0f,
    .frameBudgetMs = 66.7f,
    .core1HeadroomMs = 0.0f,
    .core1HeadroomPct = 100.0f,
    .core0LoadPct = 0.0f,
    .sensorReadMs = 0.0f,
    .sensorIntervalMs = 62.5f,
    .core0HeadroomPct = 100.0f
};

void logFrameProfile(int64_t scaleUs, int64_t flushUs, int64_t frameTotalUs, int64_t frameIntervalUs) {
    float curScaleMs = (float)scaleUs / 1000.0f;
    float curFlushMs = (float)flushUs / 1000.0f;
    float curTotalMs = (float)frameTotalUs / 1000.0f;
    float curBudgetMs = (float)frameIntervalUs / 1000.0f;

    if (curBudgetMs < 1.0f) curBudgetMs = 66.7f; // fallback ~15 fps

    float curLoadPct = (curTotalMs / curBudgetMs) * 100.0f;
    if (curLoadPct > 100.0f) curLoadPct = 100.0f;
    if (curLoadPct < 0.0f) curLoadPct = 0.0f;

    float curHeadroomMs = (curBudgetMs > curTotalMs) ? (curBudgetMs - curTotalMs) : 0.0f;
    float curHeadroomPct = 100.0f - curLoadPct;

    // Smooth via Exponential Moving Average (alpha = 0.15)
    static bool initDone = false;
    if (!initDone) {
        globalComputeStats.scaleTimeMs = curScaleMs;
        globalComputeStats.flushTimeMs = curFlushMs;
        globalComputeStats.frameTotalMs = curTotalMs;
        globalComputeStats.frameBudgetMs = curBudgetMs;
        globalComputeStats.core1LoadPct = curLoadPct;
        globalComputeStats.core1HeadroomMs = curHeadroomMs;
        globalComputeStats.core1HeadroomPct = curHeadroomPct;
        initDone = true;
    } else {
        const float alpha = 0.15f;
        globalComputeStats.scaleTimeMs += alpha * (curScaleMs - globalComputeStats.scaleTimeMs);
        globalComputeStats.flushTimeMs += alpha * (curFlushMs - globalComputeStats.flushTimeMs);
        globalComputeStats.frameTotalMs += alpha * (curTotalMs - globalComputeStats.frameTotalMs);
        globalComputeStats.frameBudgetMs += alpha * (curBudgetMs - globalComputeStats.frameBudgetMs);
        globalComputeStats.core1LoadPct += alpha * (curLoadPct - globalComputeStats.core1LoadPct);
        globalComputeStats.core1HeadroomMs += alpha * (curHeadroomMs - globalComputeStats.core1HeadroomMs);
        globalComputeStats.core1HeadroomPct += alpha * (curHeadroomPct - globalComputeStats.core1HeadroomPct);
    }
}

void logSensorProfile(int64_t sensorActiveUs, int64_t sensorIntervalUs) {
    float curActiveMs = (float)sensorActiveUs / 1000.0f;
    float curIntervalMs = (float)sensorIntervalUs / 1000.0f;

    if (curIntervalMs < 1.0f) curIntervalMs = 62.5f; // fallback ~16 Hz

    float curLoadPct = (curActiveMs / curIntervalMs) * 100.0f;
    if (curLoadPct > 100.0f) curLoadPct = 100.0f;
    if (curLoadPct < 0.0f) curLoadPct = 0.0f;

    float curHeadroomPct = 100.0f - curLoadPct;

    static bool initDone0 = false;
    if (!initDone0) {
        globalComputeStats.sensorReadMs = curActiveMs;
        globalComputeStats.sensorIntervalMs = curIntervalMs;
        globalComputeStats.core0LoadPct = curLoadPct;
        globalComputeStats.core0HeadroomPct = curHeadroomPct;
        initDone0 = true;
    } else {
        const float alpha = 0.15f;
        globalComputeStats.sensorReadMs += alpha * (curActiveMs - globalComputeStats.sensorReadMs);
        globalComputeStats.sensorIntervalMs += alpha * (curIntervalMs - globalComputeStats.sensorIntervalMs);
        globalComputeStats.core0LoadPct += alpha * (curLoadPct - globalComputeStats.core0LoadPct);
        globalComputeStats.core0HeadroomPct += alpha * (curHeadroomPct - globalComputeStats.core0HeadroomPct);
    }
}

void printComputeHeadroomReport() {
    ScalingMode curMode = getScalingMode();
    const char* modeNames[5] = {
        "Nearest Neighbor",
        "Bilinear",
        "Bicubic (Catmull-Rom)",
        "Lanczos-3",
        "TAAU (Temporal AA)"
    };

    Serial.println("\n=================== ESP32-S3 COMPUTE & HEADROOM AUDIT ===================");

    // Core 1 (Graphics, Scaling, LVGL)
    Serial.printf("[CORE 1: GRAPHICS & RENDERING]\n");
    Serial.printf("  Active Scaling Mode:       %s\n", modeNames[(int)curMode % 5]);
    Serial.printf("  Algorithm Execution Time:  %.2f ms / frame\n", globalComputeStats.scaleTimeMs);
    Serial.printf("  LVGL Draw & SPI DMA Flush: %.2f ms / frame\n", globalComputeStats.flushTimeMs);
    Serial.printf("  Total Active Frame Time:   %.2f ms\n", globalComputeStats.frameTotalMs);
    Serial.printf("  Available Frame Budget:    %.2f ms (%.1f FPS)\n", globalComputeStats.frameBudgetMs, displayFps);
    Serial.printf("  Core 1 Compute Load:       %.1f %%\n", globalComputeStats.core1LoadPct);
    Serial.printf("  --> UNUSED COMPUTE HEADROOM: %.2f ms / frame (%.1f %% Headroom)\n",
                  globalComputeStats.core1HeadroomMs, globalComputeStats.core1HeadroomPct);
    if (globalComputeStats.core1HeadroomMs > 15.0f) {
        Serial.printf("      Status: GENEROUS (Can add up to %.1f ms of heavy features without dropping FPS)\n",
                      globalComputeStats.core1HeadroomMs);
    } else if (globalComputeStats.core1HeadroomMs > 5.0f) {
        Serial.printf("      Status: MODERATE (Room for lightweight overlays or filters)\n");
    } else {
        Serial.printf("      Status: SATURATED (Frame time near budget; additional compute will reduce FPS)\n");
    }

    // Core 0 (Sensor, I2C, Controls)
    Serial.printf("\n[CORE 0: SENSOR & SYSTEM I/O]\n");
    Serial.printf("  MLX90640 I2C Read Time:    %.2f ms / frame (800 kHz I2C bus)\n", globalComputeStats.sensorReadMs);
    Serial.printf("  Sensor Frame Interval:     %.2f ms\n", globalComputeStats.sensorIntervalMs);
    Serial.printf("  Core 0 Sensor Load:        %.1f %%\n", globalComputeStats.core0LoadPct);
    Serial.printf("  --> UNUSED COMPUTE HEADROOM: %.1f %% Headroom\n", globalComputeStats.core0HeadroomPct);
    Serial.println("      Status: VERY HIGH (Core 0 is idle between I2C bursts; ideal for Wi-Fi/SD logging)");

    // Memory Headroom
    Serial.printf("\n[MEMORY & RAM HEADROOM]\n");
    Serial.printf("  Internal SRAM (DRAM):      %u KB free / %u KB total (Min watermark: %u KB)\n",
                  ESP.getFreeHeap() / 1024, ESP.getHeapSize() / 1024, ESP.getMinFreeHeap() / 1024);
    Serial.printf("  Largest Contiguous DRAM:   %u KB (Heap fragmentation check)\n",
                  ESP.getMaxAllocHeap() / 1024);
    if (psramFound()) {
        Serial.printf("  External PSRAM:            %u KB free / %u KB total (Max block: %u KB)\n",
                      ESP.getFreePsram() / 1024, ESP.getPsramSize() / 1024, ESP.getMaxAllocPsram() / 1024);
    } else {
        Serial.printf("  External PSRAM:            Not detected\n");
    }
    Serial.printf("  Flash Firmware Headroom:   %u KB free for code growth\n",
                  ESP.getFreeSketchSpace() / 1024);

    // FreeRTOS Task Stack Headroom
    Serial.printf("\n[FREERTOS TASK STACK HEADROOM (High-Water Marks)]\n");
    if (displayTaskHandle) {
        UBaseType_t hw = uxTaskGetStackHighWaterMark(displayTaskHandle);
        Serial.printf("  Display_Task (Stack %u B): %u bytes remaining unused\n",
                      DISPLAY_TASK_STACK, (unsigned int)(hw * sizeof(StackType_t)));
    }
    if (sensorTaskHandle) {
        UBaseType_t hw = uxTaskGetStackHighWaterMark(sensorTaskHandle);
        Serial.printf("  Sensor_Task  (Stack %u B): %u bytes remaining unused\n",
                      SENSOR_TASK_STACK, (unsigned int)(hw * sizeof(StackType_t)));
    }
    if (wheelTaskHandle) {
        UBaseType_t hw = uxTaskGetStackHighWaterMark(wheelTaskHandle);
        Serial.printf("  Wheel_Task   (Stack %u B): %u bytes remaining unused\n",
                      WHEEL_TASK_STACK, (unsigned int)(hw * sizeof(StackType_t)));
    }
    if (ledTaskHandle) {
        UBaseType_t hw = uxTaskGetStackHighWaterMark(ledTaskHandle);
        Serial.printf("  LED_Task     (Stack %u B): %u bytes remaining unused\n",
                      LED_TASK_STACK, (unsigned int)(hw * sizeof(StackType_t)));
    }
    Serial.println("=========================================================================\n");
}

static const char* getResetReasonStr(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "Power-on (cold boot)";
        case ESP_RST_EXT:       return "External reset pin";
        case ESP_RST_SW:        return "Software restart";
        case ESP_RST_PANIC:     return "Exception / Panic crash";
        case ESP_RST_INT_WDT:   return "Interrupt Watchdog";
        case ESP_RST_TASK_WDT:  return "Task Watchdog";
        case ESP_RST_WDT:       return "Other Watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep-sleep wakeup";
        case ESP_RST_BROWNOUT:  return "Brownout (voltage drop)";
        case ESP_RST_SDIO:      return "SDIO reset";
        default:                return "Unknown";
    }
}

static const char* getFlashModeStr(FlashMode_t mode) {
    switch (mode) {
        case FM_QIO:  return "QIO (Quad I/O)";
        case FM_QOUT: return "QOUT (Quad Output)";
        case FM_DIO:  return "DIO (Dual I/O)";
        case FM_DOUT: return "DOUT (Dual Output)";
        default:      return "Unknown";
    }
}

void diagnosticsMessageTask(void *pvParameters) {
    Serial.println("\n================ ESP32-S3 SYSTEM DIAGNOSTIC ================");

    // 1. Chip & CPU Details
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint64_t chipId = ESP.getEfuseMac();
    Serial.printf("[SOC / CPU]\n");
    Serial.printf("  Model:               %s (Revision %d)\n", ESP.getChipModel(), chip_info.revision);
    Serial.printf("  MAC / Unique ID:     %04X%08X\n", (uint16_t)(chipId >> 32), (uint32_t)chipId);
    Serial.printf("  CPU Cores:           %d\n", chip_info.cores);
    Serial.printf("  CPU Frequency:       %u MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("  Diagnostic Task Core: Core %d\n", xPortGetCoreID());
    Serial.printf("  ESP-IDF Version:     %s\n", esp_get_idf_version());

    // 2. Boot & Environmental Diagnostics
    Serial.printf("\n[BOOT & ENVIRONMENT]\n");
    Serial.printf("  Reset Reason:        %s\n", getResetReasonStr(esp_reset_reason()));
    Serial.printf("  Internal Die Temp:   %.1f C (%.1f F)\n", temperatureRead(), (temperatureRead() * 1.8f) + 32.0f);
    Serial.printf("  Uptime:              %lu ms\n", millis());

    // 3. Print Live Compute & Headroom Audit
    printComputeHeadroomReport();

    // 4. Flash Memory & Partitions
    Serial.printf("\n[FLASH & PARTITIONS]\n");
    Serial.printf("  Flash Size:          %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("  Flash Bus Speed:     %u MHz\n", ESP.getFlashChipSpeed() / 1000000);
    Serial.printf("  Flash Access Mode:   %s\n", getFlashModeStr(ESP.getFlashChipMode()));
    Serial.printf("  Sketch Code Used:    %u KB\n", ESP.getSketchSize() / 1024);
    Serial.printf("  Sketch Free Space:   %u KB\n", ESP.getFreeSketchSpace() / 1024);

    // 5. Wireless (Wi-Fi & RF Validation)
    Serial.printf("\n[RF & WI-FI TRANSCEIVER]\n");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.printf("  Wi-Fi STA MAC:       %s\n", WiFi.macAddress().c_str());
    delay(100);

    Serial.println("  Scanning 2.4 GHz Wi-Fi networks...");
    int networkCount = WiFi.scanNetworks();

    if (networkCount == 0) {
        Serial.println("  Result: No networks found (check antenna connection).");
    } else {
        Serial.printf("  Result: Found %d network(s):\n", networkCount);
        for (int i = 0; i < networkCount; ++i) {
            Serial.printf("    %2d: %-22s (RSSI: %3d dBm, Ch: %2d, Enc: %s)\n",
                          i + 1, 
                          WiFi.SSID(i).c_str(), 
                          WiFi.RSSI(i), 
                          WiFi.channel(i),
                          WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Secured");
        }
    }
    Serial.println("============================================================\n");

    // Clear handle and delete task to cleanly finish
    diagTaskHandle = NULL;
    vTaskDelete(NULL);
}
