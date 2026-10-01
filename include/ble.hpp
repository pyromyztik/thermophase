#pragma once
#include <Arduino.h>

// Initializes the BLE subsystem with the specified advertising name (defaults to "thermophase")
bool initBLE(const char* deviceName = "thermophase");

// Returns true if BLE subsystem is enabled
bool isBLEEnabled();

// Enables or disables BLE advertising and active connections
void setBLEEnabled(bool enable);

// Returns description or MAC address of connected client (or nullptr if disconnected)
const char* getBLEConnectedClientInfo();

// Returns true if a BLE client (PC, mobile app, etc.) is currently connected
bool isBLEConnected();

// Transmits formatted text over BLE NUS TX (if connected)
void blePrintf(const char* fmt, ...);

// Transmits raw string or byte buffer over BLE NUS TX (if connected)
void blePrint(const char* str);
void bleWrite(const uint8_t* data, size_t len);

// Checks for incoming bytes received from BLE RX and reads them
int bleAvailable();
int bleRead();

// Unified dual-output logging: writes to USB Serial AND to BLE simultaneously
void outPrintf(const char* fmt, ...);
void outPrint(const char* str);
void outPrintln(const char* str = "");

// Live Thermal Frame BLE Streaming Controls
bool isBLEStreaming();
void setBLEStreaming(bool enable);
void streamThermalFrameBLE(const float* temps, size_t count, float minTemp, float maxTemp, uint32_t frameIdx);

// Pulsed Phase Thermography (PPT) Asynchronous Spectral BLE Transmission
void triggerPPTBLETransfer();
bool isBLETransferringPPT();

