#include <util.hpp>

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

    // 3. Memory & Heap Analysis (Fragmentation & Watermarks)
    Serial.printf("\n[SRAM / HEAP HEALTH]\n");
    Serial.printf("  Total Heap Size:     %u KB\n", ESP.getHeapSize() / 1024);
    Serial.printf("  Current Free Heap:   %u KB (%u bytes)\n", ESP.getFreeHeap() / 1024, ESP.getFreeHeap());
    Serial.printf("  Lowest Free (Peak):  %u KB (Historical minimum)\n", ESP.getMinFreeHeap() / 1024);
    Serial.printf("  Max Allocatable Blk: %u KB (Heap fragmentation check)\n", ESP.getMaxAllocHeap() / 1024);
    Serial.printf("  Task Stack Headroom: %u bytes\n", (unsigned int)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

    // 4. PSRAM Validation
    Serial.printf("\n[PSRAM]\n");
    if (psramFound()) {
        Serial.printf("  Status:              Detected & Active\n");
        Serial.printf("  Total Size:          %u MB\n", ESP.getPsramSize() / (1024 * 1024));
        Serial.printf("  Free PSRAM:          %u KB\n", ESP.getFreePsram() / 1024);
        Serial.printf("  Max Allocatable Blk: %u KB\n", ESP.getMaxAllocPsram() / 1024);
    } else 
        Serial.println("  Status:              Not detected / Not enabled");

    // 5. Flash Memory & Partitions
    Serial.printf("\n[FLASH & PARTITIONS]\n");
    Serial.printf("  Flash Size:          %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("  Flash Bus Speed:     %u MHz\n", ESP.getFlashChipSpeed() / 1000000);
    Serial.printf("  Flash Access Mode:   %s\n", getFlashModeStr(ESP.getFlashChipMode()));
    Serial.printf("  Sketch Code Used:    %u KB\n", ESP.getSketchSize() / 1024);
    Serial.printf("  Sketch Free Space:   %u KB\n", ESP.getFreeSketchSpace() / 1024);

    // 6. Wireless (Wi-Fi & RF Validation)
    Serial.printf("\n[RF & WI-FI TRANSCEIVER]\n");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.printf("  Wi-Fi STA MAC:       %s\n", WiFi.macAddress().c_str());
    delay(100);

    Serial.println("  Scanning 2.4 GHz Wi-Fi networks...");
    int networkCount = WiFi.scanNetworks();

    if (networkCount == 0) {
        Serial.println("  Result: No networks found (check antenna connection if using IPEX/U.FL).");
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

    // Clear handle and delete task to avoid FreeRTOS panic
    diagTaskHandle = NULL;
    vTaskDelete(NULL);
}

void ledAnimationTask(void *pvParameters) {
    uint32_t hue = 0;
    while (true) {
        uint32_t color = onboardLED.gamma32(onboardLED.ColorHSV(hue));
        onboardLED.setPixelColor(0, color);
        onboardLED.show();
        hue += 256;
        if (hue >= 65536) {
            hue = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(15)); // Non-blocking FreeRTOS delay
    }
}