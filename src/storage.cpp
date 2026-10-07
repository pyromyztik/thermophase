#include "storage.hpp"
#include "ui.hpp"
#include "display.hpp"
#include "sensor.hpp"
#include "config.hpp"
#include "ble.hpp"
#include <Preferences.h>

static bool settingsDirty = false;
static const char* NVS_NAMESPACE = "thermal_cam";

void markSettingsDirty() {
    settingsDirty = true;
}

bool isSettingsDirty() {
    return settingsDirty;
}

void loadSettings() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) { // Read-only mode
        Serial.println("[STORAGE] No saved settings partition found, using system defaults.");
        return;
    }

    uint8_t bright = prefs.getUChar("brightness", TFT_DEFAULT_BRIGHTNESS);
    uint8_t dispRot = prefs.getUChar("disp_rot", DISPLAY_ROTATION);
    uint8_t unit   = prefs.getUChar("temp_unit", (uint8_t)TEMP_UNIT_CELSIUS);
    uint8_t pal    = prefs.getUChar("palette", (uint8_t)PALETTE_IRONBOW);
    bool mirror    = prefs.getBool("mirror", MIRROR_HORIZONTAL);
    uint8_t scale  = prefs.getUChar("scaling", (uint8_t)SCALING_BICUBIC);
    uint32_t i2cSpd = prefs.getUInt("i2c_speed", I2C_SPEED_800K);
    bool rgbEnabled = prefs.getBool("rgb_led", true);
    bool liveDenoise = prefs.getBool("live_denoise", true);
    bool bleEnabled = prefs.getBool("ble_enabled", true);
    uint8_t rangeMode = prefs.getUChar("range_mode", (uint8_t)THERMAL_RANGE_AUTO);
    float manMin = prefs.getFloat("man_min", 20.0f);
    float manMax = prefs.getFloat("man_max", 50.0f);
    prefs.end();

    // Clamp & sanitize values against potential flash corruption
    if (bright < 10 || bright > 100) bright = TFT_DEFAULT_BRIGHTNESS;
    if (dispRot > 1) dispRot = DISPLAY_ROTATION;
    if (unit > 2) unit = (uint8_t)TEMP_UNIT_CELSIUS;
    if (pal > 2) pal = (uint8_t)PALETTE_IRONBOW;
    if (scale > 4) scale = (uint8_t)SCALING_BICUBIC;
    if (i2cSpd != I2C_SPEED_400K && i2cSpd != I2C_SPEED_800K) i2cSpd = I2C_SPEED_800K;
    if (rangeMode > 1) rangeMode = (uint8_t)THERMAL_RANGE_AUTO;
    if (manMin < -40.0f || manMin > 200.0f) manMin = 20.0f;
    if (manMax < -30.0f || manMax > 300.0f || manMax <= manMin + 1.0f) manMax = manMin + 30.0f;

    // Apply to UI and hardware engines atomically
    setDisplayBrightnessSetting(bright);
    setDisplayRotationSetting(dispRot);
    setTempUnitSetting((TempUnit)unit);
    setPaletteSetting((PaletteType)pal);
    setMirrorSetting(mirror);
    setScalingModeSetting((ScalingMode)scale);
    setI2CSpeedSetting(i2cSpd);
    setRgbLedEnabledSetting(rgbEnabled);
    setLiveDenoiseSetting(liveDenoise);
    setBLEEnabledSetting(bleEnabled);
    setThermalRangeModeSetting((ThermalRangeMode)rangeMode);
    setManualMinTempSetting(manMin);
    setManualMaxTempSetting(manMax);

    settingsDirty = false;
    Serial.printf("[STORAGE] Loaded settings from NVS -> Brightness: %d%%, Rotation: %s, Unit: %d, Palette: %d, Mirror: %d, Scaling: %d, I2C: %lu Hz, RGB: %s, BLE: %s, Range: %s (%.1f-%.1f C)\n",
                  bright, (dispRot == 1) ? "180" : "Normal", unit, pal, mirror ? 1 : 0, scale, i2cSpd, rgbEnabled ? "ON" : "OFF", bleEnabled ? "ON" : "OFF", (rangeMode == 1) ? "MANUAL" : "AUTO", manMin, manMax);
}

void saveSettingsIfDirty() {
    if (!settingsDirty) {
        return; // Zero flash writes when no settings were altered
    }

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) { // Read/Write mode
        Serial.println("[STORAGE] ERROR: Failed to open NVS namespace for writing!");
        return;
    }

    prefs.putUChar("brightness", getDisplayBrightnessSetting());
    prefs.putUChar("disp_rot", getDisplayRotationSetting());
    prefs.putUChar("temp_unit", (uint8_t)getTempUnitSetting());
    prefs.putUChar("palette", (uint8_t)getPaletteSetting());
    prefs.putBool("mirror", getMirrorSetting());
    prefs.putUChar("scaling", (uint8_t)getScalingModeSetting());
    prefs.putUInt("i2c_speed", getI2CSpeedSetting());
    prefs.putBool("rgb_led", getRgbLedEnabledSetting());
    prefs.putBool("live_denoise", getLiveDenoiseSetting());
    prefs.putBool("ble_enabled", getBLEEnabledSetting());
    prefs.putUChar("range_mode", (uint8_t)getThermalRangeModeSetting());
    prefs.putFloat("man_min", getManualMinTempSetting());
    prefs.putFloat("man_max", getManualMaxTempSetting());
    prefs.end();

    settingsDirty = false;
    Serial.println("[STORAGE] Settings successfully committed to NVS upon menu exit.");
}

void resetSettingsToDefaults() {
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) { // Read/Write mode
        prefs.clear();
        prefs.end();
        Serial.println("[STORAGE] NVS preferences partition cleared.");
    }

    // Apply defaults to UI and hardware engines atomically
    setDisplayBrightnessSetting(TFT_DEFAULT_BRIGHTNESS);
    setDisplayRotationSetting(DISPLAY_ROTATION);
    setTempUnitSetting(TEMP_UNIT_CELSIUS);
    setPaletteSetting(PALETTE_IRONBOW);
    setMirrorSetting(MIRROR_HORIZONTAL);
    setScalingModeSetting(SCALING_BICUBIC);
    setI2CSpeedSetting(I2C_SPEED_800K);
    setRgbLedEnabledSetting(true);
    setLiveDenoiseSetting(true);
    setBLEEnabledSetting(true);
    setThermalRangeModeSetting(THERMAL_RANGE_AUTO);
    setManualMinTempSetting(20.0f);
    setManualMaxTempSetting(50.0f);

    settingsDirty = false;
    Serial.println("[STORAGE] All settings restored to factory defaults.");
}
