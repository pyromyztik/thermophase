#include "ui.hpp"
#include "display.hpp"
#include "sensor.hpp"
#include "power.hpp"
#include "ppt.hpp"
#include "storage.hpp"
#include "rgb-led.hpp"
#include "ble.hpp"

// Active application state
static AppState currentAppState = APP_STATE_BOOT;

// Runtime setting variables
static TempUnit currentTempUnitSetting = TEMP_UNIT_CELSIUS;
static PaletteType currentPaletteSetting = PALETTE_IRONBOW;
static bool currentMirrorSetting = MIRROR_HORIZONTAL;
static ScalingMode currentScalingSetting = SCALING_BICUBIC;
static uint8_t currentBrightnessSetting = TFT_DEFAULT_BRIGHTNESS;
static uint8_t currentDisplayRotationSetting = DISPLAY_ROTATION;
static uint32_t currentI2CSpeedSetting = I2C_FREQ_RUN;
static bool currentRgbLedSetting = true;
static bool currentLiveDenoiseSetting = true;
static bool currentBLESetting = true;
static ThermalRangeMode currentThermalRangeMode = THERMAL_RANGE_AUTO;
static float manualMinTemp = 20.0f;
static float manualMaxTemp = 50.0f;

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
    lv_style_set_radius(&style_btn_default, 0);
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

    // Back Button Focused Style (Filled with red, red border, black text)
    lv_style_init(&style_btn_back_focused);
    lv_style_set_bg_color(&style_btn_back_focused, UI_COLOR_ACCENT);
    lv_style_set_bg_opa(&style_btn_back_focused, LV_OPA_COVER);
    lv_style_set_border_color(&style_btn_back_focused, UI_COLOR_ACCENT);
    lv_style_set_border_width(&style_btn_back_focused, 2);
    lv_style_set_outline_width(&style_btn_back_focused, 0);
    lv_style_set_text_color(&style_btn_back_focused, lv_color_black());
    lv_style_set_transition(&style_btn_back_focused, &trans_focus);
}

// -------------------------------------------------------------
// BATTERY & WIRELESS UI INDICATOR MANAGEMENT
// -------------------------------------------------------------
struct BatteryIndicatorEntry {
    lv_obj_t* icon;
    lv_obj_t* pct;
    lv_obj_t* ble;
    bool bleRight;
};
static constexpr size_t MAX_BATTERY_INDICATORS = 8;
static BatteryIndicatorEntry batteryIndicators[MAX_BATTERY_INDICATORS];
static size_t numBatteryIndicators = 0;

// LUCIDE BLUETOOTH ICON (PUA U+E000) - Font definition
static LV_ATTRIBUTE_LARGE_CONST const uint8_t ble_icon_glyph_bitmap[] = {
    /* U+E000 */
    0x0, 0x14, 0x0, 0x0, 0x4, 0xf6, 0x0, 0x0,
    0x4c, 0xd6, 0xa, 0x64, 0xb4, 0xf1, 0x1d, 0xbc,
    0xe6, 0x0, 0x1d, 0xf6, 0x0, 0x1, 0xdf, 0x60,
    0x1, 0xdb, 0xde, 0x60, 0xa6, 0x4b, 0x4f, 0x10,
    0x4, 0xcd, 0x70, 0x0, 0x4f, 0x60, 0x0, 0x1,
    0x50, 0x0
};

static const lv_font_fmt_txt_glyph_dsc_t ble_icon_glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 192, .box_w = 7, .box_h = 12, .ofs_x = 3, .ofs_y = 0}
};

static const lv_font_fmt_txt_cmap_t ble_icon_cmaps[] = {
    {
        .range_start = 57344, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

#if LVGL_VERSION_MAJOR == 8
static lv_font_fmt_txt_glyph_cache_t ble_icon_cache;
#endif

static const lv_font_fmt_txt_dsc_t ble_icon_font_dsc = {
    .glyph_bitmap = ble_icon_glyph_bitmap,
    .glyph_dsc = ble_icon_glyph_dsc,
    .cmaps = ble_icon_cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &ble_icon_cache
#endif
};

const lv_font_t ui_font_bluetooth = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 12,
    .base_line = 0,
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &ble_icon_font_dsc,
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};

// ANTIALIASED CIRCLE ICON (PUA U+E001) - 15x15 matching Montserrat 14 play/stop symbols
static LV_ATTRIBUTE_LARGE_CONST const uint8_t circle_icon_glyph_bitmap[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x9c, 0xec, 0x93, 0x00, 0x00, 0x00,
    0x07, 0xff, 0xff, 0xff, 0xf7, 0x00, 0x00, 0x07,
    0xff, 0xff, 0xff, 0xff, 0xf7, 0x00, 0x03, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xf3, 0x00, 0x9f, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x90, 0x0c, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xfc, 0x00, 0xef, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xe0, 0x0c, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xfc, 0x00, 0x9f, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x90, 0x03, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xf3, 0x00, 0x07, 0xff, 0xff, 0xff, 0xff,
    0xf7, 0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0xf7,
    0x00, 0x00, 0x00, 0x03, 0x9c, 0xec, 0x93, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00
};

static const lv_font_fmt_txt_glyph_dsc_t circle_icon_glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 196, .box_w = 15, .box_h = 15, .ofs_x = -1, .ofs_y = -1}
};

static const lv_font_fmt_txt_cmap_t circle_icon_cmaps[] = {
    {
        .range_start = 57345, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

#if LVGL_VERSION_MAJOR == 8
static lv_font_fmt_txt_glyph_cache_t circle_icon_cache;
#endif

static const lv_font_fmt_txt_dsc_t circle_icon_font_dsc = {
    .glyph_bitmap = circle_icon_glyph_bitmap,
    .glyph_dsc = circle_icon_glyph_dsc,
    .cmaps = circle_icon_cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &circle_icon_cache
#endif
};

const lv_font_t ui_font_circle = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 16,
    .base_line = 3,
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &circle_icon_font_dsc,
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};

// Spacing offset between Bluetooth icon and battery percentage/icon
static constexpr lv_coord_t BLE_ICON_SPACING_X = -4;

void createBatteryIndicator(lv_obj_t* parent, bool showPercentage, lv_align_t align, lv_coord_t x_ofs, lv_coord_t y_ofs, bool bleRight) {
    if (!parent || numBatteryIndicators >= MAX_BATTERY_INDICATORS) return;

    // Battery Icon Label
    lv_obj_t* icon = lv_label_create(parent);
    lv_label_set_text(icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(icon, UI_COLOR_TEXT, 0);
    lv_obj_align(icon, align, x_ofs, y_ofs);

    lv_obj_t* pct = nullptr;
    if (showPercentage) {
        pct = lv_label_create(parent);
        lv_label_set_text(pct, "--%");
        lv_obj_set_style_text_font(pct, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(pct, UI_COLOR_TEXT, 0);
        if (bleRight) {
            lv_obj_align_to(pct, icon, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
        } else {
            lv_obj_align_to(pct, icon, LV_ALIGN_OUT_LEFT_MID, -4, 0);
        }
    }

    // Bluetooth Icon Label (when bleRight is true: to the right of battery indicator; else to the left)
    lv_obj_t* ble = lv_label_create(parent);
    lv_label_set_text(ble, UI_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(ble, &ui_font_bluetooth, 0);
    lv_obj_set_style_text_color(ble, UI_COLOR_TEXT_DIM, 0);
    if (bleRight) {
        if (pct != nullptr) {
            lv_obj_align_to(ble, pct, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
        } else {
            lv_obj_align_to(ble, icon, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
        }
    } else {
        if (pct != nullptr) {
            lv_obj_align_to(ble, pct, LV_ALIGN_OUT_LEFT_MID, BLE_ICON_SPACING_X, 0);
        } else {
            lv_obj_align_to(ble, icon, LV_ALIGN_OUT_LEFT_MID, BLE_ICON_SPACING_X, 0);
        }
    }
    if (!currentBLESetting) {
        lv_obj_add_flag(ble, LV_OBJ_FLAG_HIDDEN);
    }
    batteryIndicators[numBatteryIndicators++] = {icon, pct, ble, bleRight};
}

static uint32_t lastBatteryUIUpdateTime = 0;
static BatteryState lastBatteryUIState = (BatteryState)255;
static bool lastBLEUIOn = false;
static bool lastBLEUIConn = false;

void updateBatteryUI(bool force) {
    // 1. Take a sample every 1000ms to maintain the rolling 30-second average window
    sampleBattery();

    BatteryState state = getBatteryState();
    uint32_t now = millis();

    bool bleOn = currentBLESetting && isBLEEnabled();
    bool bleConn = isBLEConnected();
    bool bleChanged = (bleOn != lastBLEUIOn) || (bleConn != lastBLEUIConn);

    // 2. Only update UI widgets every 30 seconds (30000ms), UNLESS:
    //    - force is true (e.g. cold boot)
    //    - operational state changed (e.g. charger plugged in / switch flipped)
    //    - BLE status changed (connected/disconnected/toggled)
    if (!force && !bleChanged && (state == lastBatteryUIState) && (now - lastBatteryUIUpdateTime < 30000)) {
        return;
    }

    lastBatteryUIUpdateTime = now;
    lastBatteryUIState = state;
    lastBLEUIOn = bleOn;
    lastBLEUIConn = bleConn;

    if (bleChanged && currentAppState == APP_STATE_MAIN_MENU) {
        refreshMainMenuBLEState(false);
    }

    uint8_t pct = getBatteryPercentage();

    const char* sym = LV_SYMBOL_BATTERY_FULL;
    char pctStr[12] = "";
    lv_color_t col = UI_COLOR_TEXT;

    if (state == BATTERY_DISCONNECTED) {
        sym = LV_SYMBOL_USB;
        snprintf(pctStr, sizeof(pctStr), "USB");
        col = UI_COLOR_CYAN;
    } else if (state == BATTERY_CHARGING) {
        sym = LV_SYMBOL_CHARGE;
        snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
        col = UI_COLOR_GREEN;
    } else {
        // Normal discharging battery operation
        if (pct < 15) {
            sym = LV_SYMBOL_BATTERY_EMPTY;
            col = UI_COLOR_ACCENT;
        } else if (pct < 35) {
            sym = LV_SYMBOL_BATTERY_1;
            col = UI_COLOR_YELLOW;
        } else if (pct < 65) {
            sym = LV_SYMBOL_BATTERY_2;
            col = UI_COLOR_TEXT;
        } else if (pct < 90) {
            sym = LV_SYMBOL_BATTERY_3;
            col = UI_COLOR_TEXT;
        } else {
            sym = LV_SYMBOL_BATTERY_FULL;
            col = UI_COLOR_TEXT;
        }
        snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
    }

    lv_color_t bleCol = bleConn ? lv_color_hex(0x2196F3) : UI_COLOR_TEXT_DIM; // Bright blue when connected!

    for (size_t i = 0; i < numBatteryIndicators; i++) {
        if (batteryIndicators[i].icon != nullptr && lv_obj_is_valid(batteryIndicators[i].icon)) {
            lv_label_set_text(batteryIndicators[i].icon, sym);
            lv_obj_set_style_text_color(batteryIndicators[i].icon, col, 0);
        }
        if (batteryIndicators[i].pct != nullptr && lv_obj_is_valid(batteryIndicators[i].pct)) {
            lv_label_set_text(batteryIndicators[i].pct, pctStr);
            lv_obj_set_style_text_color(batteryIndicators[i].pct, col, 0);
            if (batteryIndicators[i].icon != nullptr && lv_obj_is_valid(batteryIndicators[i].icon)) {
                if (batteryIndicators[i].bleRight) {
                    lv_obj_align_to(batteryIndicators[i].pct, batteryIndicators[i].icon, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
                } else {
                    lv_obj_align_to(batteryIndicators[i].pct, batteryIndicators[i].icon, LV_ALIGN_OUT_LEFT_MID, -4, 0);
                }
            }
        }
        if (batteryIndicators[i].ble != nullptr && lv_obj_is_valid(batteryIndicators[i].ble)) {
            if (bleOn) {
                lv_obj_clear_flag(batteryIndicators[i].ble, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_text_color(batteryIndicators[i].ble, bleCol, 0);
                if (batteryIndicators[i].bleRight) {
                    if (batteryIndicators[i].pct != nullptr && lv_obj_is_valid(batteryIndicators[i].pct)) {
                        lv_obj_align_to(batteryIndicators[i].ble, batteryIndicators[i].pct, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
                    } else if (batteryIndicators[i].icon != nullptr && lv_obj_is_valid(batteryIndicators[i].icon)) {
                        lv_obj_align_to(batteryIndicators[i].ble, batteryIndicators[i].icon, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
                    }
                } else {
                    if (batteryIndicators[i].pct != nullptr && lv_obj_is_valid(batteryIndicators[i].pct)) {
                        lv_obj_align_to(batteryIndicators[i].ble, batteryIndicators[i].pct, LV_ALIGN_OUT_LEFT_MID, BLE_ICON_SPACING_X, 0);
                    } else if (batteryIndicators[i].icon != nullptr && lv_obj_is_valid(batteryIndicators[i].icon)) {
                        lv_obj_align_to(batteryIndicators[i].ble, batteryIndicators[i].icon, LV_ALIGN_OUT_LEFT_MID, BLE_ICON_SPACING_X, 0);
                    }
                }
            } else {
                lv_obj_add_flag(batteryIndicators[i].ble, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
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
    buildPPTScreen();
    buildMainMenuScreen();
    buildSettingsScreen();
    buildBrightnessScreen();
    buildScalingScreen();
    buildResetScreen();
    buildThermalRangeScreen();
    buildErrorScreen();

    // Initial state: Boot Screen
    currentAppState = APP_STATE_BOOT;
    lv_scr_load(screen_boot);

    // Register autonomous 1000ms LVGL sampling timer and perform initial forced UI refresh
    lv_timer_create([](lv_timer_t*) { updateBatteryUI(); }, 1000, NULL);
    updateBatteryUI(true);

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
TempUnit getTempUnitSetting() { return currentTempUnitSetting; }
PaletteType getPaletteSetting() { return currentPaletteSetting; }
bool getMirrorSetting() { return currentMirrorSetting; }
ScalingMode getScalingModeSetting() { return currentScalingSetting; }
uint8_t getDisplayBrightnessSetting() { return currentBrightnessSetting; }
uint8_t getDisplayRotationSetting() { return currentDisplayRotationSetting; }

void setTempUnitSetting(TempUnit unit) {
    if (currentTempUnitSetting != unit) {
        currentTempUnitSetting = unit;
        updateTempUnit(unit);
        markSettingsDirty();
    }
}
void setPaletteSetting(PaletteType pal) {
    if (currentPaletteSetting != pal) {
        currentPaletteSetting = pal;
        updatePalette(pal);
        markSettingsDirty();
    }
}
void setMirrorSetting(bool mirror) {
    if (currentMirrorSetting != mirror) {
        currentMirrorSetting = mirror;
        updateMirror(mirror);
        markSettingsDirty();
    }
}
void setScalingModeSetting(ScalingMode mode) {
    if (currentScalingSetting != mode) {
        currentScalingSetting = mode;
        updateScalingMode(mode);
        markSettingsDirty();
    }
}
void setDisplayBrightnessSetting(uint8_t percent) {
    if (currentBrightnessSetting != percent) {
        currentBrightnessSetting = percent;
        setDisplayBrightness(percent);
        markSettingsDirty();
    }
}
void setDisplayRotationSetting(uint8_t rot) {
    uint8_t normalized = (rot != 0) ? 1 : 0;
    if (currentDisplayRotationSetting != normalized) {
        currentDisplayRotationSetting = normalized;
        setDisplayRotation(normalized);
        markSettingsDirty();
    }
}
uint32_t getI2CSpeedSetting() {
    return currentI2CSpeedSetting;
}
void setI2CSpeedSetting(uint32_t speedHz) {
    if (currentI2CSpeedSetting != speedHz) {
        currentI2CSpeedSetting = speedHz;
        setSensorI2CSpeed(speedHz);
        markSettingsDirty();
    }
}
bool getRgbLedEnabledSetting() {
    return currentRgbLedSetting;
}
void setRgbLedEnabledSetting(bool enabled) {
    if (currentRgbLedSetting != enabled) {
        currentRgbLedSetting = enabled;
        markSettingsDirty();
        if (!enabled) {
            onboardLED.clear();
            onboardLED.setPixelColor(0, 0);
            onboardLED.show();
        }
    }
}
bool getLiveDenoiseSetting() {
    return currentLiveDenoiseSetting;
}
void setLiveDenoiseSetting(bool enabled) {
    if (currentLiveDenoiseSetting != enabled) {
        currentLiveDenoiseSetting = enabled;
        setLiveDenoiseEnabled(enabled);
        markSettingsDirty();
    }
}
bool getBLEEnabledSetting() {
    return currentBLESetting;
}
void setBLEEnabledSetting(bool enabled) {
    if (currentBLESetting != enabled) {
        currentBLESetting = enabled;
        setBLEEnabled(enabled);
        markSettingsDirty();
        updateBatteryUI(true);
    }
}
ThermalRangeMode getThermalRangeModeSetting() {
    return currentThermalRangeMode;
}
void setThermalRangeModeSetting(ThermalRangeMode mode) {
    if (currentThermalRangeMode != mode) {
        currentThermalRangeMode = mode;
        markSettingsDirty();
    }
}
float getManualMinTempSetting() {
    return manualMinTemp;
}
void setManualMinTempSetting(float minC) {
    if (manualMinTemp != minC) {
        manualMinTemp = minC;
        markSettingsDirty();
    }
}
float getManualMaxTempSetting() {
    return manualMaxTemp;
}
void setManualMaxTempSetting(float maxC) {
    if (manualMaxTemp != maxC) {
        manualMaxTemp = maxC;
        markSettingsDirty();
    }
}

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

void computeEffectiveThermalRange(float sceneMin, float sceneMax, float& outEffectiveMin, float& outRange) {
    if (getThermalRangeModeSetting() == THERMAL_RANGE_MANUAL) {
        float manualMin = getManualMinTempSetting();
        float manualMax = getManualMaxTempSetting();
        if (manualMax <= manualMin + 1.0f) {
            manualMax = manualMin + 1.0f;
        }
        outEffectiveMin = manualMin;
        outRange = manualMax - manualMin;
    } else {
        float span = sceneMax - sceneMin;
        if (span < THERMAL_MIN_RANGE_SPAN) {
            float mid = 0.5f * (sceneMin + sceneMax);
            outRange = THERMAL_MIN_RANGE_SPAN;
            outEffectiveMin = mid - 0.5f * THERMAL_MIN_RANGE_SPAN;
        } else {
            outEffectiveMin = sceneMin;
            outRange = span;
        }
    }
}

void formatTempStr(char* buf, size_t bufSize, float tempC, TempUnit unit, int decimals, bool includeUnit) {
    float val = convertTemp(tempC, unit);
    const char* sym = getUnitSym(unit);
    if (unit == TEMP_UNIT_KELVIN) {
        if (includeUnit) {
            if (decimals == 0) snprintf(buf, bufSize, "%.0f K", val);
            else snprintf(buf, bufSize, "%.1f K", val);
        } else {
            if (decimals == 0) snprintf(buf, bufSize, "%.0f", val);
            else snprintf(buf, bufSize, "%.1f", val);
        }
    } else {
        if (includeUnit) {
            if (decimals == 0) snprintf(buf, bufSize, "%.0f %s", val, sym);
            else snprintf(buf, bufSize, "%.1f %s", val, sym);
        } else {
            if (decimals == 0) snprintf(buf, bufSize, "%.0f" UI_SYMBOL_DEGREE, val);
            else snprintf(buf, bufSize, "%.1f" UI_SYMBOL_DEGREE, val);
        }
    }
}

const char* getScalingModeShortName(ScalingMode mode) {
    static const char* const s_names[5] = {"NEAR", "BILIN", "BICUB", "LANCZ", "TAAU"};
    return s_names[(int)mode % 5];
}

const char* getScalingModeLongName(ScalingMode mode) {
    static const char* const s_longNames[5] = {
        "Nearest Neighbor", "Bilinear", "Bicubic (Catmull-Rom)", "Lanczos-3", "TAAU (Temporal AA)"
    };
    return s_longNames[(int)mode % 5];
}

const char* getPaletteName(PaletteType pal) {
    static const char* const s_palNames[3] = {"Ironbow", "White-Hot", "Rainbow"};
    return s_palNames[(int)pal % 3];
}

// -------------------------------------------------------------
// NAVIGATION TRANSITIONS WITH ANIMATIONS
// -------------------------------------------------------------
void uiNavToThermal() {
    currentAppState = APP_STATE_THERMAL_VIEW;
    pptDisarm();
    resetScalerHistory();
    // Restore user-configured live streaming I2C speed (e.g. 800 kHz / 32 Hz)
    setSensorI2CSpeed(currentI2CSpeedSetting);
    lv_group_remove_all_objs(ui_wheel_group);
    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }

    // On initial boot transition to live feed, calibrate battery under real operational load
    static bool initialLoadPrimed = false;
    if (!initialLoadPrimed) {
        initialLoadPrimed = true;
        reprimeBatteryUnderLoad();
        updateBatteryUI(true);
    }

    // Quick crossfade (100 ms) between thermal feed and main menu
    lv_scr_load_anim(screen_thermal, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, false);
    Serial.println("[UI] Transitioned to Live Thermal View (100ms Crossfade)");
}

void uiFreezeThermal() {
    currentAppState = APP_STATE_THERMAL_FREEZE;
    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }
    uiTriggerSnapshotFlash();
    Serial.println("[UI] Thermal feed FROZEN (Snapshot captured)");
}

void uiUnfreezeThermal() {
    currentAppState = APP_STATE_THERMAL_VIEW;
    resetScalerHistory();
    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }
    Serial.println("[UI] Thermal feed RESUMED (Live view)");
}

bool isThermalFrozen() {
    return (currentAppState == APP_STATE_THERMAL_FREEZE);
}

static uint32_t pptArmedCooldownUntil = 0;

bool isPPTArmedCooldown() {
    return (millis() < pptArmedCooldownUntil);
}

void uiNavToPPT() {
    currentAppState = APP_STATE_PPT_ARMED;
    pptArm();
    resetScalerHistory();
    // Engage 400 kHz (16 Hz sensor rate) for maximum PPT signal integrity and zero bit flips
    setSensorI2CSpeed(I2C_SPEED_400K);
    pptArmedCooldownUntil = millis() + 400; // 400ms cooldown ignores button click bounce/bleed-through
    lv_group_remove_all_objs(ui_wheel_group);
    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }

    lv_scr_load_anim(screen_ppt, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, false);
    Serial.println("[UI] Transitioned to Dedicated PPT Screen (Look-Around Mode, 100ms Crossfade)");
}

void uiNavToMainMenu() {
    AppState prevState = currentAppState;
    currentAppState = APP_STATE_MAIN_MENU;

    if (uiIsFullscreen()) {
        uiExitFullscreen();
    }

    // Persist settings if exiting Settings or its dedicated sub-pages
    if (prevState == APP_STATE_SETTINGS || prevState == APP_STATE_SCALING_MENU || prevState == APP_STATE_BRIGHTNESS || prevState == APP_STATE_RESET_CONFIRM || prevState == APP_STATE_THERMAL_RANGE) {
        saveSettingsIfDirty();
    }

    pptDisarm();
    // Restore user-configured live streaming I2C speed if exiting PPT
    if (prevState == APP_STATE_PPT_ARMED || prevState == APP_STATE_PPT_ACQUIRING || prevState == APP_STATE_PPT_INSPECT) {
        setSensorI2CSpeed(currentI2CSpeedSetting);
    }
    refreshMainMenuBLEState(true);

    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }

    if (prevState == APP_STATE_SETTINGS || prevState == APP_STATE_SCALING_MENU || prevState == APP_STATE_BRIGHTNESS || prevState == APP_STATE_RESET_CONFIRM || prevState == APP_STATE_THERMAL_RANGE) {
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
        if (focusIndex == SETTING_BLUETOOTH) {
            char bleDesc[140];
            if (!getBLEEnabledSetting()) {
                snprintf(bleDesc, sizeof(bleDesc), "Bluetooth is OFF (saves ~35-45 mA / ~12%% battery). Click to enable wireless telemetry.");
            } else if (isBLEConnected()) {
                const char* client = getBLEConnectedClientInfo();
                snprintf(bleDesc, sizeof(bleDesc), "Connected to: %s. Streaming live battery & thermal telemetry.", (client && client[0]) ? client : "Device");
            } else {
                snprintf(bleDesc, sizeof(bleDesc), "Bluetooth ON: Advertising as 'thermophase'. Streams live telemetry to Web Terminal.");
            }
            lv_label_set_text(settings_desc_label, bleDesc);
        } else {
            lv_label_set_text(settings_desc_label, settingsDescriptions[focusIndex]);
        }
    }
    if (settings_list != nullptr) {
        if (focusIndex == 0) {
            lv_obj_scroll_to_y(settings_list, 0, LV_ANIM_OFF);
        } else {
            lv_obj_scroll_to_view(settingsBtns[focusIndex], LV_ANIM_OFF);
        }
    }

    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }

    if (prevState == APP_STATE_SCALING_MENU || prevState == APP_STATE_BRIGHTNESS || prevState == APP_STATE_RESET_CONFIRM || prevState == APP_STATE_THERMAL_RANGE) {
        // Subpage back to settings: slide right
        lv_scr_load_anim(screen_settings, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, false);
    } else {
        // Main menu to settings: slide left
        lv_scr_load_anim(screen_settings, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);
    }
    Serial.println("[UI] Transitioned to Settings (100ms Slide)");
}

void uiNavToResetConfirm() {
    AppState prevState = currentAppState;
    currentAppState = APP_STATE_RESET_CONFIRM;
    lv_group_remove_all_objs(ui_wheel_group);

    setupResetGroup();

    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }

    // Slide left into confirmation screen
    lv_scr_load_anim(screen_reset, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);
    Serial.println("[UI] Transitioned to Reset Confirmation Screen (100ms Slide)");
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
    updateScalingDesc(initialFocus);
    if (scaling_list != nullptr) {
        if (initialFocus == 0) {
            lv_obj_scroll_to_y(scaling_list, 0, LV_ANIM_OFF);
        } else {
            lv_obj_scroll_to_view(scalingBtns[initialFocus], LV_ANIM_OFF);
        }
    }

    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }

    // Slide left into subpage
    lv_scr_load_anim(screen_scaling, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);
    Serial.println("[UI] Transitioned to Scaling Menu (100ms Slide)");
}

static AppState s_rangeCaller = APP_STATE_THERMAL_VIEW;

void uiNavToThermalRange(AppState caller) {
    s_rangeCaller = caller;
    AppState prevState = currentAppState;
    currentAppState = APP_STATE_THERMAL_RANGE;
    lv_group_remove_all_objs(ui_wheel_group);

    setupThermalRangeGroup();

    WheelEventType dummy;
    while (getWheelEvent(dummy, 0)) { }

    // Slide left into thermal range subpage
    lv_scr_load_anim(screen_thermal_range, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);
    Serial.println("[UI] Transitioned to Thermal Range Menu (100ms Slide)");
}

AppState getThermalRangeCaller() {
    return s_rangeCaller;
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

