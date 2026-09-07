#include "ui.hpp"
#include "display.hpp"
#include "power.hpp"

// Active application state
static AppState currentAppState = APP_STATE_BOOT;

// Runtime setting variables
static int currentRotationSetting = CAMERA_ROTATION;
static TempUnit currentTempUnitSetting = TEMP_UNIT_CELSIUS;
static PaletteType currentPaletteSetting = PALETTE_IRONBOW;
static bool currentMirrorSetting = MIRROR_HORIZONTAL;
static ScalingMode currentScalingSetting = SCALING_BICUBIC;
static uint8_t currentBrightnessSetting = TFT_DEFAULT_BRIGHTNESS;

// Screen pointers
lv_obj_t* screen_boot = nullptr;
lv_obj_t* screen_thermal = nullptr;
lv_obj_t* screen_main_menu = nullptr;
lv_obj_t* screen_settings = nullptr;
lv_obj_t* screen_brightness = nullptr;
lv_obj_t* screen_scaling = nullptr;
lv_obj_t* screen_error = nullptr;

// Navigation group for rotary wheel
lv_group_t* ui_wheel_group = nullptr;

// Reusable Styles
lv_style_t style_screen;
lv_style_t style_header_title;
lv_style_t style_divider;
lv_style_t style_footer_text;
lv_style_t style_btn_default;
lv_style_t style_btn_focused;
lv_style_t style_btn_pressed;
lv_style_t style_btn_back_focused;

// -------------------------------------------------------------
// STYLES INITIALIZATION
// -------------------------------------------------------------
static void buildStyles() {
    // Pure black screen background
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, UI_COLOR_BG);
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
    lv_style_set_border_width(&style_screen, 0);
    lv_style_set_pad_all(&style_screen, 0);

    // Header Title Style
    lv_style_init(&style_header_title);
    lv_style_set_text_color(&style_header_title, UI_COLOR_TEXT);
    lv_style_set_text_font(&style_header_title, &lv_font_montserrat_16);

    // Header & Footer Divider Line Style
    lv_style_init(&style_divider);
    lv_style_set_line_color(&style_divider, UI_COLOR_SEPARATOR);
    lv_style_set_line_width(&style_divider, 1);
    lv_style_set_line_rounded(&style_divider, false);

    // Footer Hint Text Style
    lv_style_init(&style_footer_text);
    lv_style_set_text_color(&style_footer_text, UI_COLOR_TEXT_DIM);
    lv_style_set_text_font(&style_footer_text, &lv_font_montserrat_10);

    // Button Default Style (Minimalist OLED border)
    lv_style_init(&style_btn_default);
    lv_style_set_bg_color(&style_btn_default, UI_COLOR_BG);
    lv_style_set_bg_opa(&style_btn_default, LV_OPA_COVER);
    lv_style_set_border_color(&style_btn_default, UI_COLOR_BORDER);
    lv_style_set_border_width(&style_btn_default, 1);
    lv_style_set_radius(&style_btn_default, 3);
    lv_style_set_pad_hor(&style_btn_default, 10);
    lv_style_set_pad_ver(&style_btn_default, 8);
    lv_style_set_text_color(&style_btn_default, UI_COLOR_TEXT);
    lv_style_set_text_font(&style_btn_default, &lv_font_montserrat_14);

    // Button Focused Style (Dark grey highlight around selected items)
    static lv_style_transition_dsc_t trans_focus;
    static const lv_style_prop_t props[] = {
        LV_STYLE_BORDER_COLOR, LV_STYLE_BORDER_WIDTH, LV_STYLE_OUTLINE_WIDTH, 
        LV_STYLE_OUTLINE_COLOR, LV_STYLE_TEXT_COLOR, LV_STYLE_PROP_INV
    };
    lv_style_transition_dsc_init(&trans_focus, props, lv_anim_path_ease_out, 120, 0, NULL);

    lv_style_init(&style_btn_focused);
    lv_style_set_border_color(&style_btn_focused, UI_COLOR_ACCENT);
    lv_style_set_border_width(&style_btn_focused, 2);
    lv_style_set_outline_color(&style_btn_focused, UI_COLOR_FOCUS_GREY);
    lv_style_set_outline_width(&style_btn_focused, 2);
    lv_style_set_outline_pad(&style_btn_focused, 1);
    lv_style_set_outline_opa(&style_btn_focused, LV_OPA_COVER);
    lv_style_set_transition(&style_btn_focused, &trans_focus);

    // Button Pressed Style
    lv_style_init(&style_btn_pressed);
    lv_style_set_bg_color(&style_btn_pressed, UI_COLOR_ACCENT_DIM);
    lv_style_set_bg_opa(&style_btn_pressed, LV_OPA_40);
    lv_style_set_border_color(&style_btn_pressed, UI_COLOR_ACCENT);
    lv_style_set_border_width(&style_btn_pressed, 2);

    // Back Button Focused Style (Filled with dark grey, red border, accent text)
    lv_style_init(&style_btn_back_focused);
    lv_style_set_bg_color(&style_btn_back_focused, UI_COLOR_FOCUS_GREY);
    lv_style_set_bg_opa(&style_btn_back_focused, LV_OPA_COVER);
    lv_style_set_border_color(&style_btn_back_focused, UI_COLOR_ACCENT);
    lv_style_set_border_width(&style_btn_back_focused, 2);
    lv_style_set_outline_width(&style_btn_back_focused, 0);
    lv_style_set_text_color(&style_btn_back_focused, UI_COLOR_ACCENT);
    lv_style_set_transition(&style_btn_back_focused, &trans_focus);
}

// -------------------------------------------------------------
// UI INITIALIZATION
// -------------------------------------------------------------
void initUI() {
    // Override default theme to grey primary palette (eliminates default blue highlights)
    lv_theme_t* th = lv_theme_default_init(
        lv_disp_get_default(),
        lv_palette_main(LV_PALETTE_GREY),
        lv_palette_main(LV_PALETTE_RED),
        true,
        &lv_font_montserrat_14
    );
    lv_disp_set_theme(lv_disp_get_default(), th);

    buildStyles();

    // Create default rotary wheel input group
    ui_wheel_group = lv_group_create();

    // Build all functional screens
    buildBootScreen();
    buildThermalScreen();
    buildMainMenuScreen();
    buildSettingsScreen();
    buildBrightnessScreen();
    buildScalingScreen();
    buildErrorScreen();

    // Initial state: Boot Screen
    currentAppState = APP_STATE_BOOT;
    lv_scr_load(screen_boot);

    Serial.println("[UI] LVGL UI initialized successfully with animations.");
}

AppState getAppState() {
    return currentAppState;
}

void setAppState(AppState newState) {
    if (currentAppState != newState) {
        currentAppState = newState;
    }
}

// Settings Accessors and Mutators
int getCameraRotationSetting() { return currentRotationSetting; }
TempUnit getTempUnitSetting() { return currentTempUnitSetting; }
PaletteType getPaletteSetting() { return currentPaletteSetting; }
bool getMirrorSetting() { return currentMirrorSetting; }
ScalingMode getScalingModeSetting() { return currentScalingSetting; }
uint8_t getDisplayBrightnessSetting() { return currentBrightnessSetting; }

void setCameraRotationSetting(int rot) { currentRotationSetting = rot; }
void setTempUnitSetting(TempUnit unit) { currentTempUnitSetting = unit; }
void setPaletteSetting(PaletteType pal) { currentPaletteSetting = pal; }
void setMirrorSetting(bool mirror) { currentMirrorSetting = mirror; }
void setScalingModeSetting(ScalingMode mode) { currentScalingSetting = mode; }
void setDisplayBrightnessSetting(uint8_t percent) { currentBrightnessSetting = percent; }

// Temperature conversion helpers
float convertTemp(float tempC, TempUnit unit) {
    if (unit == TEMP_UNIT_FAHRENHEIT) {
        return (tempC * 1.8f) + 32.0f;
    } else if (unit == TEMP_UNIT_KELVIN) {
        return tempC + 273.15f;
    }
    return tempC;
}

const char* getUnitSym(TempUnit unit) {
    if (unit == TEMP_UNIT_FAHRENHEIT) return UI_SYMBOL_DEGREE "F";
    if (unit == TEMP_UNIT_KELVIN) return "K";
    return UI_SYMBOL_DEGREE "C";
}

const char* getUnitFullName(TempUnit unit) {
    if (unit == TEMP_UNIT_FAHRENHEIT) return "Fahrenheit";
    if (unit == TEMP_UNIT_KELVIN) return "Kelvin";
    return "Celsius";
}

// -------------------------------------------------------------
// NAVIGATION TRANSITIONS WITH ANIMATIONS
// -------------------------------------------------------------
void uiNavToThermal() {
    currentAppState = APP_STATE_THERMAL_VIEW;
    lv_group_remove_all_objs(ui_wheel_group);
    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }

    // Quick crossfade (100 ms) between thermal feed and main menu
    lv_scr_load_anim(screen_thermal, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, false);
    Serial.println("[UI] Transitioned to Live Thermal View (100ms Crossfade)");
}

void uiNavToMainMenu() {
    AppState prevState = currentAppState;
    currentAppState = APP_STATE_MAIN_MENU;
    lv_group_remove_all_objs(ui_wheel_group);

    for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
        lv_group_add_obj(ui_wheel_group, mainMenuBtns[i]);
    }
    for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
        if (i == 0) {
            lv_obj_clear_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_group_focus_obj(mainMenuBtns[0]);

    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }

    if (prevState == APP_STATE_SETTINGS || prevState == APP_STATE_SCALING_MENU) {
        // Between menu screens: fast slide transition (100ms)
        lv_scr_load_anim(screen_main_menu, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, false);
    } else {
        // Between thermal feed and main menu: quick crossfade (100ms)
        lv_scr_load_anim(screen_main_menu, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, false);
    }
    Serial.println("[UI] Transitioned to Main Menu");
}

void uiNavToSettings(int focusIndex) {
    AppState prevState = currentAppState;
    currentAppState = APP_STATE_SETTINGS;
    lv_group_remove_all_objs(ui_wheel_group);

    for (int i = 0; i < SETTINGS_COUNT; ++i) {
        lv_group_add_obj(ui_wheel_group, settingsBtns[i]);
    }
    if (focusIndex < 0 || focusIndex >= SETTINGS_COUNT) focusIndex = 0;
    for (int i = 0; i < SETTINGS_COUNT; ++i) {
        if (i == focusIndex) {
            lv_obj_clear_flag(settingsArrows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(settingsArrows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_group_focus_obj(settingsBtns[focusIndex]);
    if (settings_desc_label != nullptr) {
        lv_label_set_text(settings_desc_label, settingsDescriptions[focusIndex]);
    }
    if (settings_list != nullptr) {
        lv_obj_scroll_to_view(settingsBtns[focusIndex], LV_ANIM_OFF);
    }

    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }

    if (prevState == APP_STATE_SCALING_MENU || prevState == APP_STATE_BRIGHTNESS) {
        // Subpage back to settings: slide right
        lv_scr_load_anim(screen_settings, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, false);
    } else {
        // Main menu to settings: slide left
        lv_scr_load_anim(screen_settings, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);
    }
    Serial.println("[UI] Transitioned to Settings (100ms Slide)");
}

void uiNavToBrightness() {
    currentAppState = APP_STATE_BRIGHTNESS;
    lv_group_remove_all_objs(ui_wheel_group);

    setupBrightnessGroup();

    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }

    // Slide left into brightness subpage
    lv_scr_load_anim(screen_brightness, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);
    Serial.println("[UI] Transitioned to Brightness Menu (100ms Slide)");
}

void uiNavToScalingMenu() {
    currentAppState = APP_STATE_SCALING_MENU;
    lv_group_remove_all_objs(ui_wheel_group);

    for (int i = 0; i < SCALING_OPTIONS_COUNT; ++i) {
        lv_group_add_obj(ui_wheel_group, scalingBtns[i]);
    }
    int initialFocus = (int)currentScalingSetting;
    if (initialFocus < 0 || initialFocus >= 5) initialFocus = 0;
    for (int i = 0; i < SCALING_OPTIONS_COUNT; ++i) {
        if (i == initialFocus) {
            lv_obj_clear_flag(scalingArrows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(scalingArrows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_group_focus_obj(scalingBtns[initialFocus]);
    updateScalingValues();
    if (scaling_desc_label != nullptr) {
        lv_label_set_text(scaling_desc_label, scalingOptionDescs[initialFocus]);
    }
    if (scaling_list != nullptr) {
        lv_obj_scroll_to_view(scalingBtns[initialFocus], LV_ANIM_OFF);
    }

    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }

    // Slide left into subpage
    lv_scr_load_anim(screen_scaling, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);
    Serial.println("[UI] Transitioned to Scaling Menu (100ms Slide)");
}

void uiNavToSensorError(uint32_t attemptCount) {
    currentAppState = APP_STATE_SENSOR_ERROR;
    lv_group_remove_all_objs(ui_wheel_group);
    resetSensorErrorAttempt();
    updateSensorErrorAttempt(attemptCount);
    lv_scr_load_anim(screen_error, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, false);
}

void uiDismissSensorError() {
    resetSensorErrorAttempt();
    uiNavToThermal();
}

