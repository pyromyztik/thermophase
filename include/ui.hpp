#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include "config.hpp"
#include "wheel.hpp"

// Application UI States
enum AppState {
    APP_STATE_BOOT = 0,         // Initial system boot screen
    APP_STATE_THERMAL_VIEW,     // Live sensor view with HUD/OSD overlays (default operating state)
    APP_STATE_MAIN_MENU,        // Main menu navigation screen
    APP_STATE_SETTINGS,         // Settings & configuration screen
    APP_STATE_BRIGHTNESS,       // Dedicated display backlight brightness slider screen
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
#define UI_COLOR_ACCENT           lv_color_hex(UI_ACCENT_COLOR_HEX)
#define UI_COLOR_ACCENT_DIM       lv_color_hex(UI_ACCENT_COLOR_DIM_HEX)
#define UI_COLOR_SECONDARY_ACCENT lv_color_hex(UI_SECONDARY_ACCENT_HEX)
#define UI_COLOR_YELLOW           UI_COLOR_SECONDARY_ACCENT   // Secondary accent alias
#define UI_COLOR_FOCUS_GREY       lv_color_hex(0x404040)   // Dark grey highlight for focused items
#define UI_COLOR_SEPARATOR        lv_color_hex(0x4A4A4A)   // Crisp gray horizontal divider
#define UI_COLOR_CYAN             lv_color_hex(0x00E5FF)   // Min temp readout
#define UI_COLOR_ORANGE           lv_color_hex(0xFF9100)   // Max temp readout
#define UI_COLOR_GREEN            lv_color_hex(0x00E676)   // FPS readout

// Shared UI Styles
extern lv_style_t style_screen;
extern lv_style_t style_header_title;
extern lv_style_t style_divider;
extern lv_style_t style_footer_text;
extern lv_style_t style_btn_default;
extern lv_style_t style_btn_focused;
extern lv_style_t style_btn_pressed;
extern lv_style_t style_btn_back_focused;

// Screen objects
extern lv_obj_t* screen_boot;
extern lv_obj_t* screen_thermal;
extern lv_obj_t* screen_main_menu;
extern lv_obj_t* screen_settings;
extern lv_obj_t* screen_brightness;
extern lv_obj_t* screen_scaling;
extern lv_obj_t* screen_error;

// Screen item counts
constexpr int MAIN_MENU_COUNT = 3;
constexpr int SETTINGS_COUNT = 7;
constexpr int SCALING_OPTIONS_COUNT = 6;

// Shared Screen Widgets
extern lv_obj_t* mainMenuBtns[MAIN_MENU_COUNT];
extern lv_obj_t* mainMenuArrows[MAIN_MENU_COUNT];

extern lv_obj_t* settingsBtns[SETTINGS_COUNT];
extern lv_obj_t* settingsArrows[SETTINGS_COUNT];
extern lv_obj_t* settings_desc_label;
extern lv_obj_t* settings_list;
extern const char* settingsDescriptions[SETTINGS_COUNT];

extern lv_obj_t* scalingBtns[SCALING_OPTIONS_COUNT];
extern lv_obj_t* scalingArrows[SCALING_OPTIONS_COUNT];
extern lv_obj_t* scaling_desc_label;
extern lv_obj_t* scaling_list;
extern const char* scalingOptionDescs[SCALING_OPTIONS_COUNT];

extern lv_obj_t* error_attempt_label;

// Global LVGL rotary wheel navigation group
extern lv_group_t* ui_wheel_group;

// Core UI Lifecycle & Navigation
void initUI();
AppState getAppState();
void setAppState(AppState newState);

void uiNavToThermal();
void uiNavToMainMenu();
void uiNavToSettings(int focusIndex = 0);
void uiNavToBrightness();
void uiNavToScalingMenu();
void uiNavToSensorError(uint32_t attemptCount);
void uiDismissSensorError();

// Screen Builders
void buildBootScreen();
void buildThermalScreen();
void buildMainMenuScreen();
void buildSettingsScreen();
void buildBrightnessScreen();
void buildScalingScreen();
void buildErrorScreen();

// Screen Telemetry & Value Updaters
void updateBootScreenUI(const char* statusText, uint8_t progressPercent);
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
void updateSettingsValues();
void updateBrightnessValues();
void setupBrightnessGroup();
void updateScalingValues();
void updateSensorErrorAttempt(uint32_t attemptCount);
void resetSensorErrorAttempt();

// Accessors & Mutators
lv_obj_t* getThermalImageObj();

int getCameraRotationSetting();
TempUnit getTempUnitSetting();
PaletteType getPaletteSetting();
bool getMirrorSetting();
ScalingMode getScalingModeSetting();
uint8_t getDisplayBrightnessSetting();

void setCameraRotationSetting(int rot);
void setTempUnitSetting(TempUnit unit);
void setPaletteSetting(PaletteType pal);
void setMirrorSetting(bool mirror);
void setScalingModeSetting(ScalingMode mode);
void setDisplayBrightnessSetting(uint8_t percent);

// Shared Temperature Conversion Helpers
float convertTemp(float tempC, TempUnit unit);
const char* getUnitSym(TempUnit unit);
const char* getUnitFullName(TempUnit unit);
