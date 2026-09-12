#pragma once
#include <Arduino.h>

// Loads saved settings from ESP32 NVS (Preferences) on boot.
// If no settings are found in NVS, system defaults are preserved.
void loadSettings();

// Commits in-memory settings to NVS only if changes were made.
// Automatically resets the dirty flag upon successful write.
void saveSettingsIfDirty();

// Marks that a setting has been modified in RAM.
void markSettingsDirty();

// Returns true if unsaved modifications exist in RAM.
bool isSettingsDirty();
