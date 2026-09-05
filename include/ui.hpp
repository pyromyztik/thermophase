#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include "config.hpp"
#include "encoder.hpp"

// Application UI States
enum AppState {
    APP_STATE_BOOT = 0,         // Initial system boot screen
    APP_STATE_THERMAL_VIEW,     // Live sensor view with HUD/OSD overlays (default operating state)
    APP_STATE_MAIN_MENU,        // Main menu navigation screen
    APP_STATE_SETTINGS,         // Settings & configuration screen
    APP_STATE_SCALING_MENU,     // Dedicated scaling / interpolation mode select screen
    APP_STATE_SENSOR_ERROR      // Hardware disconnected / sensor error screen
};

// Temperature Unit Options
enum TempUnit {
    TEMP_UNIT_CELSIUS = 0,
    TEMP_UNIT_FAHRENHEIT,
    TEMP_UNIT_KELVIN
};

// Color Palette Options
enum PaletteType {
    PALETTE_IRONBOW = 0,
    PALETTE_WHITE_HOT,
    PALETTE_RAINBOW
};

// Scaling Mode Options
enum ScalingMode {
    SCALING_NEAREST = 0,
    SCALING_BILINEAR,
    SCALING_BICUBIC,
    SCALING_LANCZOS,
    SCALING_TAAU
};

// UTF-8 Degree Symbol
#define UI_SYMBOL_DEGREE     "\xC2\xB0"

// Minimalist Dark Theme Colors (matching original OLED aesthetic)
#define UI_COLOR_BG          lv_color_hex(0x000000)
#define UI_COLOR_BORDER      lv_color_hex(0xFFFFFF)
#define UI_COLOR_TEXT        lv_color_hex(0xFFFFFF)
#define UI_COLOR_TEXT_DIM    lv_color_hex(0xBDC3C7)
#define UI_COLOR_ACCENT      lv_color_hex(0xFF1E27)   // Vivid Red accent
#define UI_COLOR_ACCENT_DIM  lv_color_hex(0x8B0000)   // Deep Red for subtle glows
#define UI_COLOR_FOCUS_GREY  lv_color_hex(0x404040)   // Dark grey highlight for focused items
#define UI_COLOR_SEPARATOR   lv_color_hex(0x4A4A4A)   // Crisp gray horizontal divider
#define UI_COLOR_CYAN        lv_color_hex(0x00E5FF)   // Min temp readout
#define UI_COLOR_ORANGE      lv_color_hex(0xFF9100)   // Max temp readout
#define UI_COLOR_GREEN       lv_color_hex(0x00E676)   // FPS readout
#define UI_COLOR_YELLOW      lv_color_hex(0xFFD600)   // Thermal header readout

// Screen objects
extern lv_obj_t* screen_boot;
extern lv_obj_t* screen_thermal;
extern lv_obj_t* screen_main_menu;
extern lv_obj_t* screen_settings;
extern lv_obj_t* screen_scaling;
extern lv_obj_t* screen_error;

// Global LVGL encoder navigation group
extern lv_group_t* ui_encoder_group;

// Initializes LVGL styles, screens, groups and starts in boot screen state
void initUI();

// Returns the current UI state
AppState getAppState();

// Direct state transition
void setAppState(AppState newState);

// Navigation transitions with animations
void uiNavToThermal();
void uiNavToMainMenu();
void uiNavToSettings(int focusIndex = 0);
void uiNavToScalingMenu();
void uiNavToSensorError(uint32_t attemptCount);
void uiDismissSensorError();

// Boot Screen updater
void updateBootScreenUI(const char* statusText, uint8_t progressPercent);

// Thermal HUD & Overlay updater
void updateThermalHUD(
    float centerTemp,
    float minTemp,
    float maxTemp,
    float fps,
    ScalingMode mode,
    TempUnit unit,
    const uint16_t* activePalette,
    int vpX,
    int vpY,
    int vpW,
    int vpH
);

// Thermal feed image descriptor accessor
lv_obj_t* getThermalImageObj();

// Process encoder events for UI navigation
bool processUIInput(EncoderEventType event);

// Runtime Settings Accessors
int getCameraRotationSetting();
TempUnit getTempUnitSetting();
PaletteType getPaletteSetting();
bool getMirrorSetting();
ScalingMode getScalingModeSetting();
