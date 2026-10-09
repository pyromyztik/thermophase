#pragma once
#include <Arduino.h>
#include <cstddef>
#include <cstdint>

// Initializes the Wi-Fi streaming & Web Server subsystem
bool initWiFiStreaming();

// Returns true if Wi-Fi streaming AP and web server are currently active
bool isWiFiStreamingEnabled();

// Enables or disables the Wi-Fi SoftAP, WebServer, and WebSockets streamer
void setWiFiStreamingEnabled(bool enabled);

// Returns true if at least one client device is connected to the camera AP
bool isWiFiClientConnected();

// Returns number of connected Wi-Fi station clients
uint8_t getWiFiConnectedStationCount();

// Broadcasts an acquired thermal frame to all connected Wi-Fi WebSocket clients
void streamThermalFrameWiFi(const float* temps, size_t count, float minTemp, float maxTemp, uint8_t rangeMode = 0, uint32_t frameIdx = 0);

// SoftAP network details
const char* getWiFiAPSSID();
const char* getWiFiAPIP();
const char* getWiFiLatestClientIP();

// Active streaming state (user toggle from UI)
bool isWiFiStreamingActive();
void setWiFiStreamingActive(bool active);

// Live Web Terminal logging
void wifiTerminalLog(const char* str);
void wifiTerminalLogf(const char* fmt, ...);
