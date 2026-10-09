#include "wifi.hpp"
#include "wifi-webpage-gz.h"
#include "serial.hpp"
#include "ui.hpp"
#include "power.hpp"
#include "display.hpp"
#include "sensor.hpp"
#include "storage.hpp"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <cmath>

static WebServer* s_server = nullptr;
static WebSocketsServer* s_wsServer = nullptr;
static DNSServer s_dnsServer;
static volatile uint8_t s_connectedStations = 0;
static bool s_wifiEnabled = false;
static TaskHandle_t s_wifiTaskHandle = nullptr;

static const char* s_apSSID = "thermophase AP";
static const char* s_apIP = "192.168.4.1";

static volatile uint32_t s_wsClientCount = 0;
static bool s_userStreamingActive = false;
static char s_latestClientIP[24] = "None";

// Mutex-protected frame buffer for streaming over Wi-Fi WebSocket (allocated in PSRAM)
struct WiFiFrameBuffer {
    float temperatures[768];
    float minTemp;
    float maxTemp;
    uint8_t rangeMode;
    uint32_t frameIdx;
};

static constexpr size_t WIFI_PACKET_SIZE = 4 + 6 + 768 * sizeof(int16_t); // 1546 bytes
static WiFiFrameBuffer* s_wifiFrameBuffer = nullptr;
static WiFiFrameBuffer* s_wifiWorkerSnapshot = nullptr;
static SemaphoreHandle_t s_wifiFrameMutex = nullptr;
static volatile bool s_wifiNewFrameAvailable = false;
static uint8_t* s_wifiPacketBuffer = nullptr;

// --- Web Serial Terminal Buffer & Broadcast Queue ---
static const size_t TERM_LOG_MAX_LINES = 64;
static const size_t TERM_LOG_LINE_LEN = 160;
static char s_termLogs[TERM_LOG_MAX_LINES][TERM_LOG_LINE_LEN];
static size_t s_termLogHead = 0;
static size_t s_termLogCount = 0;
static SemaphoreHandle_t s_termLogMutex = nullptr;

static const size_t PENDING_QUEUE_SIZE = 32;
static char s_pendingBroadcast[PENDING_QUEUE_SIZE][TERM_LOG_LINE_LEN];
static size_t s_pendingHead = 0;
static size_t s_pendingTail = 0;

void wifiTerminalLog(const char* str) {
    if (!str) return;
    if (!s_termLogMutex) {
        s_termLogMutex = xSemaphoreCreateMutex();
        if (!s_termLogMutex) return;
    }

    // Filter out bare newlines
    if (strcmp(str, "\r\n") == 0 || strcmp(str, "\n") == 0 || str[0] == '\0') {
        return;
    }

    if (xSemaphoreTake(s_termLogMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Strip trailing \r\n
        char clean[TERM_LOG_LINE_LEN];
        size_t len = strlen(str);
        if (len >= TERM_LOG_LINE_LEN) len = TERM_LOG_LINE_LEN - 1;
        memcpy(clean, str, len);
        clean[len] = '\0';
        while (len > 0 && (clean[len - 1] == '\r' || clean[len - 1] == '\n')) {
            clean[--len] = '\0';
        }

        if (len > 0) {
            strncpy(s_termLogs[s_termLogHead], clean, TERM_LOG_LINE_LEN - 1);
            s_termLogs[s_termLogHead][TERM_LOG_LINE_LEN - 1] = '\0';
            s_termLogHead = (s_termLogHead + 1) % TERM_LOG_MAX_LINES;
            if (s_termLogCount < TERM_LOG_MAX_LINES) s_termLogCount++;

            size_t nextPending = (s_pendingHead + 1) % PENDING_QUEUE_SIZE;
            if (nextPending != s_pendingTail) {
                strncpy(s_pendingBroadcast[s_pendingHead], clean, TERM_LOG_LINE_LEN - 1);
                s_pendingBroadcast[s_pendingHead][TERM_LOG_LINE_LEN - 1] = '\0';
                s_pendingHead = nextPending;
            }
        }
        xSemaphoreGive(s_termLogMutex);
    }
}

void wifiTerminalLogf(const char* fmt, ...) {
    char buf[TERM_LOG_LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    wifiTerminalLog(buf);
}


static void handleRoot() {
    if (s_server) {
        s_server->sendHeader("Content-Encoding", "gzip");
        s_server->send_P(200, "text/html", (const char*)PAGE_INDEX_GZ, PAGE_INDEX_GZ_LEN);
    }
}

static void handleStatus() {
    if (!s_server) return;
    BatteryState bState = getBatteryState();
    const char* stateStr = (bState == BATTERY_CHARGING) ? "CHARGING" :
                           (bState == BATTERY_DISCHARGING) ? "DISCHARGING" : "DISCONNECTED";

    char json[640];
    snprintf(json, sizeof(json),
        "{\"spotTemp\":%.1f,\"minTemp\":%.1f,\"maxTemp\":%.1f,\"fps\":%.1f,"
        "\"scaling\":\"%s\",\"palette\":\"%s\",\"paletteId\":%u,\"unit\":\"%s\","
        "\"emissivity\":%.2f,\"emissivityPreset\":\"%s\",\"txPower\":%.1f,"
        "\"rangeMode\":%u,\"manualMin\":%.1f,\"manualMax\":%.1f,"
        "\"battery\":%u,\"voltage\":%.3f,\"pwrState\":\"%s\","
        "\"clients\":%u,\"wsClients\":%lu,\"freeHeap\":%u,\"freePsram\":%u,\"uptime\":%lu}",
        displayCenterTemp, displayMinTemp, displayMaxTemp, displayFps,
        getScalingModeShortName(getScalingModeSetting()),
        getPaletteName(getPaletteSetting()),
        (unsigned int)getPaletteSetting(),
        (getTempUnitSetting() == TEMP_UNIT_FAHRENHEIT ? "F" : (getTempUnitSetting() == TEMP_UNIT_KELVIN ? "K" : "C")),
        getEmissivitySetting(),
        getEmissivityPresetName(getEmissivitySetting()),
        (s_wifiEnabled && WiFi.getMode() == WIFI_AP) ? ((float)WiFi.getTxPower() * 0.25f) : 0.0f,
        (unsigned int)getThermalRangeModeSetting(),
        getManualMinTempSetting(),
        getManualMaxTempSetting(),
        getBatteryPercentage(), getBatteryVoltage(), stateStr,
        (s_wifiEnabled && WiFi.getMode() == WIFI_AP) ? s_connectedStations : 0,
        (unsigned long)s_wsClientCount,
        ESP.getFreeHeap(), ESP.getFreePsram(), (unsigned long)(millis() / 1000)
    );
    s_server->send(200, "application/json", json);
}

static void handleControl() {
    if (!s_server) return;
    bool changed = false;

    if (s_server->hasArg("rangeMode")) {
        int rm = s_server->arg("rangeMode").toInt();
        setThermalRangeModeSetting(rm == 1 ? THERMAL_RANGE_MANUAL : THERMAL_RANGE_AUTO);
        changed = true;
    }
    if (s_server->hasArg("manualMin")) {
        float minVal = s_server->arg("manualMin").toFloat();
        if (minVal >= -20.0f && minVal <= 298.0f) {
            setManualMinTempSetting(minVal);
            changed = true;
        }
    }
    if (s_server->hasArg("manualMax")) {
        float maxVal = s_server->arg("manualMax").toFloat();
        if (maxVal >= -18.0f && maxVal <= 300.0f) {
            setManualMaxTempSetting(maxVal);
            changed = true;
        }
    }
    if (s_server->hasArg("emissivity")) {
        float emi = s_server->arg("emissivity").toFloat();
        if (emi >= 0.05f && emi <= 1.0f) {
            setEmissivitySetting(emi);
            changed = true;
        }
    }
    if (s_server->hasArg("palette")) {
        int pal = s_server->arg("palette").toInt();
        if (pal >= 0 && pal <= 2) {
            setPaletteSetting((PaletteType)pal);
            changed = true;
        }
    }

    if (changed) {
        markSettingsDirty();
    }

    char resp[256];
    snprintf(resp, sizeof(resp),
        "{\"status\":\"ok\",\"rangeMode\":%u,\"manualMin\":%.1f,\"manualMax\":%.1f,\"emissivity\":%.2f,\"palette\":%u}",
        (unsigned int)getThermalRangeModeSetting(),
        getManualMinTempSetting(),
        getManualMaxTempSetting(),
        getEmissivitySetting(),
        (unsigned int)getPaletteSetting()
    );
    s_server->send(200, "application/json", resp);
}

static void handleLogs() {
    if (!s_server) return;
    String json = "[";
    if (s_termLogMutex && xSemaphoreTake(s_termLogMutex, pdMS_TO_TICKS(15)) == pdTRUE) {
        size_t start = (s_termLogCount == TERM_LOG_MAX_LINES) ? s_termLogHead : 0;
        for (size_t i = 0; i < s_termLogCount; ++i) {
            size_t idx = (start + i) % TERM_LOG_MAX_LINES;
            if (i > 0) json += ",";
            String line = s_termLogs[idx];
            line.replace("\\", "\\\\");
            line.replace("\"", "\\\"");
            json += "\"" + line + "\"";
        }
        xSemaphoreGive(s_termLogMutex);
    }
    json += "]";
    s_server->send(200, "application/json", json);
}

static void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            if (s_wsClientCount > 0) s_wsClientCount--;
            if (s_wsClientCount == 0) {
                snprintf(s_latestClientIP, sizeof(s_latestClientIP), "None");
            }
            Serial.printf("[WS] Client #%u disconnected. Active stream clients: %lu\n", num, (unsigned long)s_wsClientCount);
            break;
        case WStype_CONNECTED: {
            s_wsClientCount++;
            IPAddress ip = s_wsServer->remoteIP(num);
            snprintf(s_latestClientIP, sizeof(s_latestClientIP), "%s", ip.toString().c_str());
            Serial.printf("[WS] Client #%u connected from %s. Active stream clients: %lu\n",
                          num, ip.toString().c_str(), (unsigned long)s_wsClientCount);

            // Greet client and forward recent terminal log history
            s_wsServer->sendTXT(num, "[SYSTEM] Connected to Thermophase Live Web Terminal");
            if (s_termLogMutex && xSemaphoreTake(s_termLogMutex, pdMS_TO_TICKS(15)) == pdTRUE) {
                size_t start = (s_termLogCount == TERM_LOG_MAX_LINES) ? s_termLogHead : 0;
                for (size_t i = 0; i < s_termLogCount; ++i) {
                    size_t idx = (start + i) % TERM_LOG_MAX_LINES;
                    s_wsServer->sendTXT(num, s_termLogs[idx]);
                }
                xSemaphoreGive(s_termLogMutex);
            }
            break;
        }
        case WStype_TEXT: {
            // Interactive serial terminal commands from web UI
            String cmd = String((const char*)payload);
            cmd.trim();
            if (cmd.length() > 0) {
                wifiTerminalLogf("> %s", cmd.c_str());
                handleSerialCommand(cmd);
            }
            break;
        }
        default:
            break;
    }
}

static void wifiServerTask(void* pvParameters) {
    uint32_t lastBroadcastMs = 0;
    const uint32_t MIN_BROADCAST_INTERVAL_MS = 55; // Pacing: ~18 FPS max to prevent lwIP buffer backlog

    while (s_wifiEnabled) {
        s_dnsServer.processNextRequest();
        if (s_server) {
            s_server->handleClient();
        }
        if (s_wsServer) {
            s_wsServer->loop();
        }

        // Drain pending terminal log lines to active WebSocket clients
        if (s_wsServer && s_wsClientCount > 0 && s_pendingHead != s_pendingTail) {
            if (s_termLogMutex && xSemaphoreTake(s_termLogMutex, 0) == pdTRUE) {
                uint8_t drained = 0;
                while (s_pendingTail != s_pendingHead && drained < 8) {
                    s_wsServer->broadcastTXT(s_pendingBroadcast[s_pendingTail]);
                    s_pendingTail = (s_pendingTail + 1) % PENDING_QUEUE_SIZE;
                    drained++;
                }
                xSemaphoreGive(s_termLogMutex);
            }
        }


        // Broadcast pending thermal frame to WebSocket stream clients only when streaming is active
        uint32_t now = millis();
        bool pacingReady = (now - lastBroadcastMs >= MIN_BROADCAST_INTERVAL_MS);

        bool hasFrame = false;
        if (s_userStreamingActive && pacingReady && s_wifiNewFrameAvailable && s_wifiFrameMutex && s_wifiWorkerSnapshot && s_wifiFrameBuffer) {
            if (xSemaphoreTake(s_wifiFrameMutex, 0) == pdTRUE) {
                if (s_wifiNewFrameAvailable) {
                    memcpy(s_wifiWorkerSnapshot, s_wifiFrameBuffer, sizeof(WiFiFrameBuffer));
                    s_wifiNewFrameAvailable = false;
                    hasFrame = true;
                }
                xSemaphoreGive(s_wifiFrameMutex);
            }
        }

        if (hasFrame && s_wsServer && s_wsClientCount > 0 && s_wifiPacketBuffer && s_wifiWorkerSnapshot) {
            // Memory Guard: Broadcast if internal SRAM has at least 12 KB headroom
            uint32_t freeInternalHeap = esp_get_free_internal_heap_size();
            if (freeInternalHeap >= 12288) {
                lastBroadcastMs = now;

                // Pack 1546-byte packet:
                // 0x5AA55AA7 + rangeMode (1B) + reserved (1B) + minTemp (2B int16 cdeg) + maxTemp (2B int16 cdeg) + 768 int16 values
                s_wifiPacketBuffer[0] = 0x5A;
                s_wifiPacketBuffer[1] = 0xA5;
                s_wifiPacketBuffer[2] = 0x5A;
                s_wifiPacketBuffer[3] = 0xA7;
                s_wifiPacketBuffer[4] = s_wifiWorkerSnapshot->rangeMode;
                s_wifiPacketBuffer[5] = (uint8_t)getPaletteSetting(); // Active palette ID (0=Ironbow, 1=White-Hot, 2=Rainbow)

                int32_t minCdeg = (int32_t)std::round(s_wifiWorkerSnapshot->minTemp * 100.0f);
                if (minCdeg < -32768) minCdeg = -32768;
                if (minCdeg > 32767) minCdeg = 32767;
                int16_t minVal = (int16_t)minCdeg;

                int32_t maxCdeg = (int32_t)std::round(s_wifiWorkerSnapshot->maxTemp * 100.0f);
                if (maxCdeg < -32768) maxCdeg = -32768;
                if (maxCdeg > 32767) maxCdeg = 32767;
                int16_t maxVal = (int16_t)maxCdeg;

                memcpy(s_wifiPacketBuffer + 6, &minVal, sizeof(int16_t));
                memcpy(s_wifiPacketBuffer + 8, &maxVal, sizeof(int16_t));

                int16_t* pInt16 = reinterpret_cast<int16_t*>(s_wifiPacketBuffer + 10);
                for (size_t i = 0; i < 768; ++i) {
                    float val = s_wifiWorkerSnapshot->temperatures[i];
                    if (!std::isfinite(val)) val = 25.0f;
                    int32_t cdeg = (int32_t)std::round(val * 100.0f);
                    if (cdeg < -32768) cdeg = -32768;
                    if (cdeg > 32767) cdeg = 32767;
                    pInt16[i] = (int16_t)cdeg;
                }

                s_wsServer->broadcastBIN(s_wifiPacketBuffer, WIFI_PACKET_SIZE);
            } else {
                // Internal memory tight: drop frame to let TCP stack drain
                static uint32_t s_lastHeapWarn = 0;
                if (now - s_lastHeapWarn > 2000) {
                    s_lastHeapWarn = now;
                    Serial.printf("[WIFI-STREAM] Internal RAM low (%u bytes), throttling stream frame.\n", freeInternalHeap);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(4)); // Yield CPU so network stack can breathe
    }

    s_wifiTaskHandle = nullptr;
    vTaskDelete(NULL);
}

bool initWiFiStreaming() {
    if (!s_wifiFrameMutex) {
        s_wifiFrameMutex = xSemaphoreCreateMutex();
    }
    static bool s_eventsRegistered = false;
    if (!s_eventsRegistered) {
        s_eventsRegistered = true;
        WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
            if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
                s_connectedStations++;
                Serial.printf("[WIFI] Station connected (MAC: %02X:%02X:%02X:%02X:%02X:%02X). Stations: %u\n",
                              info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
                              info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
                              info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5],
                              s_connectedStations);
            } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
                if (s_connectedStations > 0) s_connectedStations--;
                Serial.printf("[WIFI] Station disconnected. Stations: %u\n", s_connectedStations);
            }
        });
    }
    return true;
}

bool isWiFiStreamingEnabled() {
    return s_wifiEnabled;
}

void setWiFiStreamingEnabled(bool enabled) {
    if (s_wifiEnabled == enabled) return;
    s_wifiEnabled = enabled;

    if (enabled) {
        if (!s_wifiFrameMutex) {
            s_wifiFrameMutex = xSemaphoreCreateMutex();
        }

        // Allocate Wi-Fi frame buffers in PSRAM (saving ~7.7 KB internal DRAM, fallback to DRAM)
        if (!s_wifiFrameBuffer) {
            s_wifiFrameBuffer = (WiFiFrameBuffer*)heap_caps_malloc(sizeof(WiFiFrameBuffer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!s_wifiFrameBuffer) s_wifiFrameBuffer = (WiFiFrameBuffer*)malloc(sizeof(WiFiFrameBuffer));
        }
        if (!s_wifiWorkerSnapshot) {
            s_wifiWorkerSnapshot = (WiFiFrameBuffer*)heap_caps_malloc(sizeof(WiFiFrameBuffer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!s_wifiWorkerSnapshot) s_wifiWorkerSnapshot = (WiFiFrameBuffer*)malloc(sizeof(WiFiFrameBuffer));
        }
        if (!s_wifiPacketBuffer) {
            s_wifiPacketBuffer = (uint8_t*)heap_caps_malloc(WIFI_PACKET_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!s_wifiPacketBuffer) s_wifiPacketBuffer = (uint8_t*)malloc(WIFI_PACKET_SIZE);
        }

        WiFi.persistent(false);
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_AP);
        delay(60);

        IPAddress local_ip(192, 168, 4, 1);
        IPAddress gateway(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);

        WiFi.softAPConfig(local_ip, gateway, subnet);

        // Start open SoftAP on channel 1, max 4 clients
        bool ok = WiFi.softAP(s_apSSID, NULL, 1, 0, 4);
        if (!ok) {
            Serial.println("[WIFI] Warning: First softAP attempt failed, retrying...");
            delay(100);
            ok = WiFi.softAP(s_apSSID, NULL, 1, 0, 4);
        }

        // Lower RF transmit power to suppress peak current surges, eliminating inductor & capacitor coil whine
        // while preserving strong signal coverage for 5-10 meters.
        WiFi.setTxPower((wifi_power_t)WIFI_TX_POWER_RAW);
        wifi_power_t curTx = WiFi.getTxPower();
        Serial.printf("[WIFI] Transmit power configured to %.1f dBm (raw %d) -> Coil whine suppressed.\n",
                      (float)curTx * 0.25f, (int)curTx);

        // Disable modem sleep to guarantee zero DTIM beacon jitter and keep phone connected rock-solid
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);

        // Start Captive Portal DNS Server on port 53 (redirects all queries to 192.168.4.1)
        s_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        s_dnsServer.start(53, "*", local_ip);

        // Initialize HTTP WebServer on port 80
        if (!s_server) {
            s_server = new WebServer(80);
            s_server->enableCORS(true);
            s_server->on("/", HTTP_GET, handleRoot);
            s_server->on("/index.html", HTTP_GET, handleRoot);
            s_server->on("/api/status", HTTP_GET, handleStatus);
            s_server->on("/api/control", HTTP_ANY, handleControl);
            s_server->on("/api/logs", HTTP_GET, handleLogs);
            s_server->on("/generate_204", HTTP_GET, [](){ if (s_server) s_server->send(204, "text/plain", ""); });
            s_server->on("/gen_204", HTTP_GET, [](){ if (s_server) s_server->send(204, "text/plain", ""); });
            s_server->on("/hotspot-detect.html", HTTP_GET, handleRoot);
            s_server->on("/canonical.html", HTTP_GET, handleRoot);
            s_server->on("/favicon.ico", HTTP_GET, [](){ if (s_server) s_server->send(204, "image/x-icon", ""); });
            s_server->onNotFound([](){
                if (!s_server) return;
                String host = s_server->hostHeader();
                if (!host.equalsIgnoreCase("192.168.4.1") && !host.startsWith("thermalcam")) {
                    s_server->sendHeader("Location", "http://192.168.4.1/", true);
                    s_server->send(302, "text/plain", "");
                    return;
                }
                s_server->send(404, "text/plain", "Not Found");
            });
        }
        s_server->begin();

        // Initialize WebSocketsServer on port 81 for real-time thermal frame streaming
        if (!s_wsServer) {
            s_wsServer = new WebSocketsServer(81);
            s_wsServer->onEvent(webSocketEvent);
        }
        s_wsServer->begin();
        s_wsClientCount = 0;

        MDNS.begin("thermalcam");
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ws", "tcp", 81);

        if (s_wifiTaskHandle == nullptr) {
            xTaskCreatePinnedToCore(
                wifiServerTask,
                "WiFi_Server_Task",
                8192,
                NULL,
                1,
                &s_wifiTaskHandle,
                0 // Core 0 alongside network stack
            );
        }

        Serial.printf("[WIFI] Access Point ACTIVE! SSID: '%s' | Web: http://%s | WebSocket: ws://%s:81 | mDNS: http://thermalcam.local\n",
                      s_apSSID, WiFi.softAPIP().toString().c_str(), WiFi.softAPIP().toString().c_str());
    } else {
        s_dnsServer.stop();
        if (s_wsServer) {
            s_wsServer->close();
            s_wsClientCount = 0;
        }
        if (s_server) {
            s_server->close();
        }
        MDNS.end();
        s_wifiEnabled = false;
        for (int i = 0; i < 20 && s_wifiTaskHandle != nullptr; ++i) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (s_wifiTaskHandle != nullptr) {
            vTaskDelete(s_wifiTaskHandle);
            s_wifiTaskHandle = nullptr;
        }
        s_connectedStations = 0;
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        Serial.println("[WIFI] Access Point stopped & Wi-Fi radio OFF (power saving active).");
    }
}

bool isWiFiClientConnected() {
    if (!s_wifiEnabled || WiFi.getMode() != WIFI_AP) return false;
    return (s_connectedStations > 0);
}

uint8_t getWiFiConnectedStationCount() {
    if (!s_wifiEnabled || WiFi.getMode() != WIFI_AP) return 0;
    return s_connectedStations;
}

bool isWiFiStreamingActive() {
    return s_wifiEnabled && s_userStreamingActive;
}

void setWiFiStreamingActive(bool active) {
    if (s_userStreamingActive == active) return;
    s_userStreamingActive = active;
    Serial.printf("[WIFI] Active streaming toggled: %s\n", active ? "ENABLED" : "DISABLED");
}

const char* getWiFiLatestClientIP() {
    return s_latestClientIP;
}

const char* getWiFiAPSSID() {
    return s_apSSID;
}

const char* getWiFiAPIP() {
    return s_apIP;
}

void streamThermalFrameWiFi(const float* temps, size_t count, float minTemp, float maxTemp, uint8_t rangeMode, uint32_t frameIdx) {
    if (!s_wifiEnabled || !s_userStreamingActive || !temps || count == 0 || !s_wifiFrameBuffer) return;

    if (s_wifiFrameMutex && xSemaphoreTake(s_wifiFrameMutex, 0) == pdTRUE) {
        size_t n = (count > 768) ? 768 : count;
        memcpy(s_wifiFrameBuffer->temperatures, temps, n * sizeof(float));
        s_wifiFrameBuffer->minTemp = minTemp;
        s_wifiFrameBuffer->maxTemp = maxTemp;
        s_wifiFrameBuffer->rangeMode = rangeMode;
        s_wifiFrameBuffer->frameIdx = frameIdx;
        s_wifiNewFrameAvailable = true;
        xSemaphoreGive(s_wifiFrameMutex);
    }
}
