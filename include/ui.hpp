#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include "config.hpp"
#include "wheel.hpp"

// Application UI States
enum AppState {
    APP_STATE_BOOT = 0,         // Initial system boot screen
    APP_STATE_THERMAL_VIEW,     // Live sensor view with HUD/OSD overlays (default operating state)
    APP_STATE_THERMAL_FREEZE,   // Frozen captured snapshot view
    APP_STATE_MAIN_MENU,        // Main menu navigation screen
    APP_STATE_SETTINGS,         // Settings & configuration screen
    APP_STATE_BRIGHTNESS,       // Dedicated display backlight brightness slider screen
    APP_STATE_EMISSIVITY,       // Dedicated surface emissivity slider & presets screen
    APP_STATE_SCALING_MENU,     // Dedicated scaling / interpolation mode select screen
    APP_STATE_RESET_CONFIRM,    // Factory reset confirmation dialogue screen
    APP_STATE_THERMAL_RANGE,    // Thermal range mode and span configuration screen
    APP_STATE_SENSOR_ERROR,     // Hardware disconnected / sensor error screen
    APP_STATE_PPT_ARMED,        // PPT Armed, awaiting thermal pulse trigger
    APP_STATE_PPT_ACQUIRING,    // PPT Recording 64 frames
    APP_STATE_PPT_INSPECT,      // PPT Phase depth slicing & frequency sweep
    APP_STATE_WIFI_STREAM       // Dedicated low-overhead Wi-Fi streaming state
};

// Thermal Range Modes (Automatic vs Manual temperature span clamping)
enum ThermalRangeMode {
    THERMAL_RANGE_AUTO = 0,
    THERMAL_RANGE_MANUAL = 1
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
#define UI_COLOR_RED              lv_color_hex(0xFF1744)   // Active streaming indicator red

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
extern lv_obj_t* screen_ppt;
extern lv_obj_t* screen_main_menu;
extern lv_obj_t* screen_settings;
extern lv_obj_t* screen_brightness;
extern lv_obj_t* screen_emissivity;
extern lv_obj_t* screen_scaling;
extern lv_obj_t* screen_reset;
extern lv_obj_t* screen_thermal_range;
extern lv_obj_t* screen_error;
extern lv_obj_t* screen_wifi_stream;

// Screen item counts
constexpr int MAIN_MENU_COUNT = 6;
constexpr int SCALING_OPTIONS_COUNT = 7;

enum SettingItemIndex {
    SETTING_BRIGHTNESS = 0,
    SETTING_ORIENTATION = 1,
    SETTING_MIRROR = 2,
    SETTING_PALETTE = 3,
    SETTING_UPSCALING = 4,
    SETTING_TEMP_UNIT = 5,
    SETTING_THERMAL_RANGE = 6,
    SETTING_EMISSIVITY = 7,
    SETTING_I2C_CLOCK = 8,
    SETTING_RGB_LED = 9,
    SETTING_BLUETOOTH = 10,
    SETTING_WIFI_STREAMING = 11,
    SETTING_FACTORY_RESET = 12,
    SETTING_BACK = 13,
    SETTINGS_COUNT = 14
};

// Bluetooth Icon Font & Symbol
#define UI_SYMBOL_BLUETOOTH "\xEE\x80\x80"
extern const lv_font_t ui_font_bluetooth;

// Antialiased Circle Icon Font & Symbol (13x13 matching Montserrat 14 play/stop)
#define UI_SYMBOL_CIRCLE    "\xEE\x80\x81"
extern const lv_font_t ui_font_circle;

// Shared Screen Widgets
extern lv_obj_t* main_menu_list;
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
void uiFreezeThermal();
void uiUnfreezeThermal();
bool isThermalFrozen();
void uiTriggerSnapshotFlash();
void uiNavToMainMenu();
void uiNavToSettings(int focusIndex = 0);
void uiNavToBrightness();
void uiNavToEmissivity();
void uiNavToScalingMenu();
void uiNavToResetConfirm();
void uiNavToThermalRange(AppState caller = APP_STATE_THERMAL_VIEW);
AppState getThermalRangeCaller();
void uiNavToSensorError(uint32_t attemptCount);
void uiDismissSensorError();
void uiNavToPPT();
bool isPPTArmedCooldown();
void uiNavToWiFiStream();
void requestUINavToWiFiStream();
void requestUINavFromWiFiStream();

// Thermal Range Knob Gesture Progress Bar Overlay
void showThermalRangeProgressBar(int currentTicks, int maxTicks);
void hideThermalRangeProgressBar(bool animate = true);

// Screen Builders
void buildBootScreen();
void buildThermalScreen();
void buildPPTScreen();
void buildMainMenuScreen();
void buildSettingsScreen();
void buildBrightnessScreen();
void buildEmissivityScreen();
void buildScalingScreen();
void buildResetScreen();
void setupResetGroup();
void buildThermalRangeScreen();
void setupThermalRangeGroup();
void buildErrorScreen();
void buildWiFiStreamScreen();
void refreshMainMenuWirelessState(bool resetFocus = false);

// Screen Telemetry & Value Updaters
void updateWiFiStreamHUD(float spotTemp, float minTemp, float maxTemp, float fps, uint32_t freePsram, const char* clientIp);
void updateSettingsDescription(int idx);
void updateBootScreenUI(const char* statusText, uint8_t progressPercent);
void updateThermalHUD(
    float centerTemp,
    float sensorMinTemp,
    float sensorMaxTemp,
    float colorMinTemp,
    float colorMaxTemp,
    float fps,
    ScalingMode mode,
    TempUnit unit,
    const uint16_t* activePalette,
    int vpX,
    int vpY,
    int vpW,
    int vpH
);
void updatePPTScreenArmed(float currentDt, float thresh, float minTemp = 0.0f, float maxTemp = 0.0f, TempUnit unit = TEMP_UNIT_CELSIUS, const uint16_t* activePalette = nullptr);
void updatePPTScreenAcquiring(int currentFrame, int totalFrames);
void updatePPTScreenInspect(
    int binK,
    float freqHz,
    float spotPhase,
    float minPhase,
    float maxPhase,
    const char* depthDesc,
    ScalingMode mode,
    const uint16_t* activePalette
);
void updateSettingsValues();
void updateBrightnessValues();
void setupBrightnessGroup();
void updateEmissivityValues();
void setupEmissivityGroup();
void updateScalingValues();
void updateScalingDesc(int idx);
void updateSensorErrorAttempt(uint32_t attemptCount);
void resetSensorErrorAttempt();

// Battery UI Indicator Management
void createBatteryIndicator(lv_obj_t* parent, bool showPercentage = true, lv_align_t align = LV_ALIGN_TOP_RIGHT, lv_coord_t x_ofs = -14, lv_coord_t y_ofs = 9, bool bleRight = false);
void updateBatteryUI(bool force = false);

// Fullscreen Thermal View Controls
void uiEnterFullscreen();
void uiExitFullscreen();
bool uiIsFullscreen();
void uiToggleFullscreen();

// Accessors & Mutators
lv_obj_t* getThermalImageObj();

TempUnit getTempUnitSetting();
PaletteType getPaletteSetting();
bool getMirrorSetting();
ScalingMode getScalingModeSetting();
uint8_t getDisplayBrightnessSetting();
uint8_t getDisplayRotationSetting();
uint32_t getI2CSpeedSetting();
bool getRgbLedEnabledSetting();
bool getLiveDenoiseSetting();
bool getBLEEnabledSetting();
bool getWiFiStreamingSetting();
ThermalRangeMode getThermalRangeModeSetting();
float getManualMinTempSetting();
float getManualMaxTempSetting();
float getEmissivitySetting();

void setTempUnitSetting(TempUnit unit);
void setPaletteSetting(PaletteType pal);
void setMirrorSetting(bool mirror);
void setScalingModeSetting(ScalingMode mode);
void setDisplayBrightnessSetting(uint8_t percent);
void setDisplayRotationSetting(uint8_t rot);
void setI2CSpeedSetting(uint32_t speedHz);
void setRgbLedEnabledSetting(bool enabled);
void setLiveDenoiseSetting(bool enabled);
void setBLEEnabledSetting(bool enabled);
void setWiFiStreamingSetting(bool enabled);
void setThermalRangeModeSetting(ThermalRangeMode mode);
void setManualMinTempSetting(float minC);
void setManualMaxTempSetting(float maxC);
void setEmissivitySetting(float emi);
const char* getEmissivityPresetName(float emi);

// Shared Temperature Conversion Helpers
float convertTemp(float tempC, TempUnit unit);
const char* getUnitSym(TempUnit unit);
void computeEffectiveThermalRange(float sceneMin, float sceneMax, float& outEffectiveMin, float& outRange);
const char* getUnitFullName(TempUnit unit);
void formatTempStr(char* buf, size_t bufSize, float tempC, TempUnit unit, int decimals = 1, bool includeUnit = true);
const char* getScalingModeShortName(ScalingMode mode);
const char* getScalingModeLongName(ScalingMode mode);
const char* getPaletteName(PaletteType pal);
