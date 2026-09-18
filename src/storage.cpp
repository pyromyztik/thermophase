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
    prefs.end();

    // Clamp & sanitize values against potential flash corruption
    if (bright < 10 || bright > 100) bright = TFT_DEFAULT_BRIGHTNESS;
    if (dispRot > 1) dispRot = DISPLAY_ROTATION;
    if (unit > 2) unit = (uint8_t)TEMP_UNIT_CELSIUS;
    if (pal > 2) pal = (uint8_t)PALETTE_IRONBOW;
    if (scale > 4) scale = (uint8_t)SCALING_BICUBIC;
    if (i2cSpd != I2C_SPEED_400K && i2cSpd != I2C_SPEED_800K) i2cSpd = I2C_SPEED_800K;

    // Apply to UI runtime state
    setDisplayBrightnessSetting(bright);
    setDisplayRotationSetting(dispRot);
    setTempUnitSetting((TempUnit)unit);
    setPaletteSetting((PaletteType)pal);
    setMirrorSetting(mirror);
    setScalingModeSetting((ScalingMode)scale);
    setI2CSpeedSetting(i2cSpd);
    setRgbLedEnabledSetting(rgbEnabled);
    setLiveDenoiseSetting(liveDenoise);
    setLiveDenoiseEnabled(liveDenoise);
    setBLEEnabledSetting(bleEnabled);

    // Apply to Display engine
    setDisplayBrightness(bright);
    setDisplayRotation(dispRot);
    updateTempUnit((TempUnit)unit);
    updatePalette((PaletteType)pal);
    updateMirror(mirror);
    updateScalingMode((ScalingMode)scale);

    // Apply to Sensor engine
    setSensorI2CSpeed(i2cSpd);

    // Apply to BLE engine
    setBLEEnabled(bleEnabled);

    settingsDirty = false;
    Serial.printf("[STORAGE] Loaded settings from NVS -> Brightness: %d%%, Rotation: %s, Unit: %d, Palette: %d, Mirror: %d, Scaling: %d, I2C: %lu Hz, RGB: %s, BLE: %s\n",
                  bright, (dispRot == 1) ? "180" : "Normal", unit, pal, mirror ? 1 : 0, scale, i2cSpd, rgbEnabled ? "ON" : "OFF", bleEnabled ? "ON" : "OFF");
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
    prefs.end();

    settingsDirty = false;
    Serial.println("[STORAGE] Settings successfully committed to NVS upon menu exit.");
}
