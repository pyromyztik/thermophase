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

// Screen pointers
lv_obj_t* screen_boot = nullptr;
lv_obj_t* screen_thermal = nullptr;
lv_obj_t* screen_main_menu = nullptr;
lv_obj_t* screen_settings = nullptr;
lv_obj_t* screen_scaling = nullptr;
lv_obj_t* screen_error = nullptr;

// Navigation group for rotary encoder
lv_group_t* ui_encoder_group = nullptr;

// Reusable Styles
static lv_style_t style_screen;
static lv_style_t style_header_title;
static lv_style_t style_divider;
static lv_style_t style_footer_text;
static lv_style_t style_btn_default;
static lv_style_t style_btn_focused;
static lv_style_t style_btn_pressed;
static lv_style_t style_btn_back_focused;

// Boot Screen Widgets
static lv_obj_t* boot_bar = nullptr;
static lv_obj_t* boot_status_label = nullptr;
static lv_obj_t* boot_pct_label = nullptr;
static lv_obj_t* boot_reticle_ring = nullptr;

// Live Thermal View Widgets
static lv_obj_t* img_thermal_feed = nullptr;
static lv_img_dsc_t thermal_img_dsc;
static lv_obj_t* thermal_reticle_container = nullptr;
static lv_obj_t* settings_list = nullptr;

// Side HUD Widgets (for 3:4 aspect ratio)
static lv_obj_t* hud_left_panel = nullptr;
static lv_obj_t* hud_right_panel = nullptr;
static lv_obj_t* label_center_temp = nullptr;
static lv_obj_t* label_temp_unit = nullptr;
static lv_obj_t* label_min_temp = nullptr;
static lv_obj_t* label_max_temp = nullptr;
static lv_obj_t* label_fps = nullptr;
static lv_obj_t* label_scaling_mode = nullptr;
static lv_obj_t* canvas_colorbar = nullptr;
static lv_color_t colorbar_cbuf[16 * 160];
static lv_obj_t* label_colorbar_max = nullptr;
static lv_obj_t* label_colorbar_min = nullptr;

// Fullscreen Overlays (for stretched mode)
static lv_obj_t* hud_fs_center_badge = nullptr;
static lv_obj_t* label_fs_center = nullptr;
static lv_obj_t* hud_fs_range_badge = nullptr;
static lv_obj_t* label_fs_min = nullptr;
static lv_obj_t* label_fs_max = nullptr;
static lv_obj_t* hud_fs_fps_badge = nullptr;
static lv_obj_t* label_fs_fps = nullptr;

// Main Menu Widgets
static const char* mainMenuLabels[] = {
    "Live Thermal View",
    "Settings",
    "Power Off"
};
static const int MAIN_MENU_COUNT = 3;
static lv_obj_t* mainMenuBtns[MAIN_MENU_COUNT];
static lv_obj_t* mainMenuArrows[MAIN_MENU_COUNT];
static int currentMenuIndex = 0;

// Settings Widgets
static const char* settingsLabels[] = {
    "Rotation",
    "Temp Unit",
    "Color Palette",
    "Mirror Feed",
    "Upscaling",
    LV_SYMBOL_LEFT "  Back to Main Menu"
};
static const char* settingsDescriptions[] = {
    "Adjust sensor orientation (0" UI_SYMBOL_DEGREE ", 90" UI_SYMBOL_DEGREE ", 180" UI_SYMBOL_DEGREE ", 270" UI_SYMBOL_DEGREE ")",
    "Select temperature unit (" UI_SYMBOL_DEGREE "C, " UI_SYMBOL_DEGREE "F, K)",
    "Select false-color palette for thermal gradients",
    "Horizontally flip thermal video stream",
    "Open upscaler menu to configure interpolation modes",
    "Return to main menu"
};
static const int SETTINGS_COUNT = 6;
static lv_obj_t* settingsBtns[SETTINGS_COUNT];
static lv_obj_t* settingsNameLabels[SETTINGS_COUNT];
static lv_obj_t* settingsValLabels[SETTINGS_COUNT];
static lv_obj_t* settingsArrows[SETTINGS_COUNT];
static lv_obj_t* settings_desc_label = nullptr;
static int currentSettingsIndex = 0;

// Scaling Subpage Widgets
static const char* scalingOptionLabels[] = {
    "Nearest Neighbor",
    "Bilinear",
    "Bicubic (Catmull-Rom)",
    "Lanczos-3",
    "TAAU (Temporal AA)",
    LV_SYMBOL_LEFT "  Back to Settings"
};
static const char* scalingOptionDescs[] = {
    "Blocky raw pixels; lowest CPU overhead & highest FPS",
    "Smooth 2x2 linear interpolation; fast performance",
    "4-tap Catmull-Rom spline; balanced sharpness & clarity",
    "6-tap Lanczos sinc filter; high edge reconstruction",
    "Temporal AA with motion reprojection & CAS sharpening",
    "Return to settings menu"
};
static const int SCALING_OPTIONS_COUNT = 6;
static lv_obj_t* scalingBtns[SCALING_OPTIONS_COUNT];
static lv_obj_t* scalingNameLabels[SCALING_OPTIONS_COUNT];
static lv_obj_t* scalingStatusLabels[SCALING_OPTIONS_COUNT];
static lv_obj_t* scalingArrows[SCALING_OPTIONS_COUNT];
static lv_obj_t* scaling_desc_label = nullptr;
static lv_obj_t* scaling_list = nullptr;
static int currentScalingMenuIndex = 0;

// Sensor Error Widgets
static lv_obj_t* error_attempt_label = nullptr;

// Forward declarations
static void buildStyles();
static void buildBootScreen();
static void buildThermalScreen();
static void buildMainMenuScreen();
static void buildSettingsScreen();
static void buildScalingScreen();
static void buildErrorScreen();
static void updateSettingsValues();
static void updateScalingValues();

// Temperature conversion helpers
static float convertTemp(float tempC, TempUnit unit) {
    if (unit == TEMP_UNIT_FAHRENHEIT) {
        return (tempC * 1.8f) + 32.0f;
    } else if (unit == TEMP_UNIT_KELVIN) {
        return tempC + 273.15f;
    }
    return tempC;
}
static const char* getUnitSym(TempUnit unit) {
    if (unit == TEMP_UNIT_FAHRENHEIT) return UI_SYMBOL_DEGREE "F";
    if (unit == TEMP_UNIT_KELVIN) return "K";
    return UI_SYMBOL_DEGREE "C";
}
static const char* getUnitFullName(TempUnit unit) {
    if (unit == TEMP_UNIT_FAHRENHEIT) return "Fahrenheit";
    if (unit == TEMP_UNIT_KELVIN) return "Kelvin";
    return "Celsius";
}

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

    // Create default encoder input group
    ui_encoder_group = lv_group_create();

    // Build all functional screens
    buildBootScreen();
    buildThermalScreen();
    buildMainMenuScreen();
    buildSettingsScreen();
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

int getCameraRotationSetting() { return currentRotationSetting; }
TempUnit getTempUnitSetting() { return currentTempUnitSetting; }
PaletteType getPaletteSetting() { return currentPaletteSetting; }
bool getMirrorSetting() { return currentMirrorSetting; }
ScalingMode getScalingModeSetting() { return currentScalingSetting; }
lv_obj_t* getThermalImageObj() { return img_thermal_feed; }

// -------------------------------------------------------------
// SCREEN 1: BOOT SCREEN
// -------------------------------------------------------------
static void anim_pulse_cb(void* var, int32_t v) {
    lv_obj_set_style_outline_pad((lv_obj_t*)var, v, 0);
}

static void buildBootScreen() {
    screen_boot = lv_obj_create(NULL);
    lv_obj_add_style(screen_boot, &style_screen, 0);
    lv_obj_clear_flag(screen_boot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_boot, LV_SCROLLBAR_MODE_OFF);

    // Aperture / Reticle Icon Emblem at (160, 52)
    boot_reticle_ring = lv_obj_create(screen_boot);
    lv_obj_set_size(boot_reticle_ring, 44, 44);
    lv_obj_align(boot_reticle_ring, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_radius(boot_reticle_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(boot_reticle_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(boot_reticle_ring, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(boot_reticle_ring, 2, 0);
    lv_obj_set_style_outline_color(boot_reticle_ring, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_outline_opa(boot_reticle_ring, LV_OPA_60, 0);

    // Animated breathing ring on aperture icon
    lv_anim_t a_ring;
    lv_anim_init(&a_ring);
    lv_anim_set_var(&a_ring, boot_reticle_ring);
    lv_anim_set_values(&a_ring, 0, 4);
    lv_anim_set_time(&a_ring, 900);
    lv_anim_set_playback_time(&a_ring, 900);
    lv_anim_set_repeat_count(&a_ring, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a_ring, anim_pulse_cb);
    lv_anim_start(&a_ring);

    // Inner bright center core
    lv_obj_t* core = lv_obj_create(boot_reticle_ring);
    lv_obj_set_size(core, 12, 12);
    lv_obj_center(core);
    lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(core, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(core, 0, 0);

    // Title: "THERMOPHASE"
    lv_obj_t* title = lv_label_create(screen_boot);
    lv_label_set_text(title, "THERMOPHASE");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 88);

    // Subtitle: "REAL-TIME THERMAL IMAGER"
    lv_obj_t* subtitle = lv_label_create(screen_boot);
    lv_label_set_text(subtitle, "REAL-TIME THERMAL IMAGER");
    lv_obj_set_style_text_color(subtitle, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_letter_space(subtitle, 1, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 114);

    // Progress Bar
    boot_bar = lv_bar_create(screen_boot);
    lv_obj_set_size(boot_bar, 220, 6);
    lv_obj_align(boot_bar, LV_ALIGN_TOP_MID, 0, 168);
    lv_obj_set_style_bg_color(boot_bar, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(boot_bar, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(boot_bar, 1, 0);
    lv_obj_set_style_radius(boot_bar, 2, 0);
    lv_obj_set_style_bg_color(boot_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(boot_bar, 2, LV_PART_INDICATOR);
    lv_bar_set_range(boot_bar, 0, 100);
    lv_bar_set_value(boot_bar, 0, LV_ANIM_OFF);

    // Status Message below bar
    boot_status_label = lv_label_create(screen_boot);
    lv_label_set_text(boot_status_label, "Initializing...");
    lv_obj_set_style_text_color(boot_status_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(boot_status_label, &lv_font_montserrat_10, 0);
    lv_obj_align(boot_status_label, LV_ALIGN_TOP_LEFT, 50, 184);

    // Percentage on right
    boot_pct_label = lv_label_create(screen_boot);
    lv_label_set_text(boot_pct_label, "0%");
    lv_obj_set_style_text_color(boot_pct_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(boot_pct_label, &lv_font_montserrat_10, 0);
    lv_obj_align(boot_pct_label, LV_ALIGN_TOP_RIGHT, -50, 184);
}

void updateBootScreenUI(const char* statusText, uint8_t progressPercent) {
    if (boot_bar == nullptr) return;
    if (progressPercent > 100) progressPercent = 100;

    lv_bar_set_value(boot_bar, progressPercent, LV_ANIM_ON);
    if (statusText != nullptr) {
        lv_label_set_text(boot_status_label, statusText);
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%u%%", progressPercent);
    lv_label_set_text(boot_pct_label, buf);

    // Force LVGL timer update to render immediately during blocking boot steps
    lv_refr_now(NULL);
}

// -------------------------------------------------------------
// SCREEN 2: LIVE THERMAL VIEW
// -------------------------------------------------------------
static void buildThermalScreen() {
    screen_thermal = lv_obj_create(NULL);
    lv_obj_add_style(screen_thermal, &style_screen, 0);
    lv_obj_clear_flag(screen_thermal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_thermal, LV_SCROLLBAR_MODE_OFF);

    // 1. Thermal image widget
    img_thermal_feed = lv_img_create(screen_thermal);
    lv_obj_set_pos(img_thermal_feed, 70, 0);
    lv_obj_set_size(img_thermal_feed, 180, 240);

    // Configure image descriptor pointing to external PSRAM buffer
    thermal_img_dsc.header.always_zero = 0;
    thermal_img_dsc.header.w = 180;
    thermal_img_dsc.header.h = 240;
    thermal_img_dsc.data_size = 180 * 240 * sizeof(lv_color_t);
    thermal_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    thermal_img_dsc.data = (const uint8_t*)getThermalFrameBuffer();
    lv_img_set_src(img_thermal_feed, &thermal_img_dsc);

    // 2. Reticle / Center Crosshair (15x15 px exact match of original design)
    thermal_reticle_container = lv_obj_create(screen_thermal);
    lv_obj_set_size(thermal_reticle_container, 15, 15);
    lv_obj_set_pos(thermal_reticle_container, (DISPLAY_WIDTH / 2) - 7, (DISPLAY_HEIGHT / 2) - 7);
    lv_obj_set_style_bg_opa(thermal_reticle_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(thermal_reticle_container, 0, 0);
    lv_obj_set_style_pad_all(thermal_reticle_container, 0, 0);
    lv_obj_clear_flag(thermal_reticle_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(thermal_reticle_container, LV_OBJ_FLAG_SCROLLABLE);

    // Reticle Horizontal Hairline (15 px white line at y = 7)
    static lv_point_t h_pts[] = {{0, 7}, {14, 7}};
    lv_obj_t* line_h = lv_line_create(thermal_reticle_container);
    lv_line_set_points(line_h, h_pts, 2);
    lv_obj_set_style_line_color(line_h, UI_COLOR_TEXT, 0);
    lv_obj_set_style_line_width(line_h, 1, 0);

    // Reticle Vertical Hairline (15 px white line at x = 7)
    static lv_point_t v_pts[] = {{7, 0}, {7, 14}};
    lv_obj_t* line_v = lv_line_create(thermal_reticle_container);
    lv_line_set_points(line_v, v_pts, 2);
    lv_obj_set_style_line_color(line_v, UI_COLOR_TEXT, 0);
    lv_obj_set_style_line_width(line_v, 1, 0);

    // Center Red Target Pixel at (7, 7)
    lv_obj_t* center_dot = lv_obj_create(thermal_reticle_container);
    lv_obj_set_size(center_dot, 1, 1);
    lv_obj_set_pos(center_dot, 7, 7);
    lv_obj_set_style_radius(center_dot, 0, 0);
    lv_obj_set_style_bg_color(center_dot, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center_dot, 0, 0);
    lv_obj_set_style_pad_all(center_dot, 0, 0);

    // 3. Side HUD Panel: LEFT (Telemetry)
    hud_left_panel = lv_obj_create(screen_thermal);
    lv_obj_set_pos(hud_left_panel, 0, 0);
    lv_obj_set_size(hud_left_panel, 70, 240);
    lv_obj_set_style_bg_color(hud_left_panel, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(hud_left_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hud_left_panel, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(hud_left_panel, 1, 0);
    lv_obj_set_style_border_side(hud_left_panel, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_pad_all(hud_left_panel, 4, 0);
    lv_obj_clear_flag(hud_left_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_thm = lv_label_create(hud_left_panel);
    lv_label_set_text(title_thm, "THERMAL");
    lv_obj_set_style_text_color(title_thm, UI_COLOR_YELLOW, 0);
    lv_obj_set_style_text_font(title_thm, &lv_font_montserrat_10, 0);
    lv_obj_align(title_thm, LV_ALIGN_TOP_LEFT, 2, 4);

    lv_obj_t* lbl_cnt = lv_label_create(hud_left_panel);
    lv_label_set_text(lbl_cnt, "CENTER");
    lv_obj_set_style_text_color(lbl_cnt, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_cnt, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_cnt, LV_ALIGN_TOP_LEFT, 2, 26);

    label_center_temp = lv_label_create(hud_left_panel);
    lv_label_set_text(label_center_temp, "--.-");
    lv_obj_set_style_text_color(label_center_temp, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_center_temp, &lv_font_montserrat_20, 0);
    lv_obj_align(label_center_temp, LV_ALIGN_TOP_LEFT, 2, 42);

    label_temp_unit = lv_label_create(hud_left_panel);
    lv_label_set_text(label_temp_unit, "Celsius");
    lv_obj_set_style_text_color(label_temp_unit, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_temp_unit, &lv_font_montserrat_10, 0);
    lv_obj_align(label_temp_unit, LV_ALIGN_TOP_LEFT, 2, 68);

    lv_obj_t* lbl_min_hdr = lv_label_create(hud_left_panel);
    lv_label_set_text(lbl_min_hdr, "MIN TEMP");
    lv_obj_set_style_text_color(lbl_min_hdr, UI_COLOR_CYAN, 0);
    lv_obj_set_style_text_font(lbl_min_hdr, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_min_hdr, LV_ALIGN_TOP_LEFT, 2, 102);

    label_min_temp = lv_label_create(hud_left_panel);
    lv_label_set_text(label_min_temp, "--.- C");
    lv_obj_set_style_text_color(label_min_temp, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_min_temp, &lv_font_montserrat_12, 0);
    lv_obj_align(label_min_temp, LV_ALIGN_TOP_LEFT, 2, 118);

    lv_obj_t* lbl_max_hdr = lv_label_create(hud_left_panel);
    lv_label_set_text(lbl_max_hdr, "MAX TEMP");
    lv_obj_set_style_text_color(lbl_max_hdr, UI_COLOR_ORANGE, 0);
    lv_obj_set_style_text_font(lbl_max_hdr, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_max_hdr, LV_ALIGN_TOP_LEFT, 2, 142);

    label_max_temp = lv_label_create(hud_left_panel);
    lv_label_set_text(label_max_temp, "--.- C");
    lv_obj_set_style_text_color(label_max_temp, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_max_temp, &lv_font_montserrat_12, 0);
    lv_obj_align(label_max_temp, LV_ALIGN_TOP_LEFT, 2, 158);

    // 4. Side HUD Panel: RIGHT (FPS, Upscaler Mode & Color Bar)
    hud_right_panel = lv_obj_create(screen_thermal);
    lv_obj_set_pos(hud_right_panel, 250, 0);
    lv_obj_set_size(hud_right_panel, 70, 240);
    lv_obj_set_style_bg_color(hud_right_panel, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(hud_right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hud_right_panel, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(hud_right_panel, 1, 0);
    lv_obj_set_style_border_side(hud_right_panel, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_pad_all(hud_right_panel, 4, 0);
    lv_obj_clear_flag(hud_right_panel, LV_OBJ_FLAG_SCROLLABLE);

    label_fps = lv_label_create(hud_right_panel);
    lv_label_set_text(label_fps, "0.0 FPS");
    lv_obj_set_style_text_color(label_fps, UI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(label_fps, &lv_font_montserrat_10, 0);
    lv_obj_align(label_fps, LV_ALIGN_TOP_LEFT, 4, 6);

    label_scaling_mode = lv_label_create(hud_right_panel);
    lv_label_set_text(label_scaling_mode, "BICUB");
    lv_obj_set_style_text_color(label_scaling_mode, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_scaling_mode, &lv_font_montserrat_10, 0);
    lv_obj_align(label_scaling_mode, LV_ALIGN_TOP_LEFT, 4, 20);

    // Vertical Color Bar Canvas (16x160)
    canvas_colorbar = lv_canvas_create(hud_right_panel);
    lv_canvas_set_buffer(canvas_colorbar, colorbar_cbuf, 16, 160, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(canvas_colorbar, 6, 36);
    lv_obj_set_style_border_color(canvas_colorbar, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(canvas_colorbar, 1, 0);

    label_colorbar_max = lv_label_create(hud_right_panel);
    lv_label_set_text(label_colorbar_max, "35");
    lv_obj_set_style_text_color(label_colorbar_max, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_colorbar_max, &lv_font_montserrat_10, 0);
    lv_obj_align(label_colorbar_max, LV_ALIGN_TOP_LEFT, 26, 36);

    label_colorbar_min = lv_label_create(hud_right_panel);
    lv_label_set_text(label_colorbar_min, "20");
    lv_obj_set_style_text_color(label_colorbar_min, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_colorbar_min, &lv_font_montserrat_10, 0);
    lv_obj_align(label_colorbar_min, LV_ALIGN_TOP_LEFT, 26, 184);

    // 5. Fullscreen Overlay Badges (Hidden initially, shown when aspect ratio stretched)
    hud_fs_center_badge = lv_obj_create(screen_thermal);
    lv_obj_set_size(hud_fs_center_badge, 110, 22);
    lv_obj_align(hud_fs_center_badge, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(hud_fs_center_badge, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(hud_fs_center_badge, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(hud_fs_center_badge, 1, 0);
    lv_obj_set_style_radius(hud_fs_center_badge, 4, 0);
    lv_obj_clear_flag(hud_fs_center_badge, LV_OBJ_FLAG_SCROLLABLE);

    label_fs_center = lv_label_create(hud_fs_center_badge);
    lv_label_set_text(label_fs_center, "+ --.- C");
    lv_obj_center(label_fs_center);
    lv_obj_set_style_text_color(label_fs_center, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_fs_center, &lv_font_montserrat_12, 0);

    hud_fs_range_badge = lv_obj_create(screen_thermal);
    lv_obj_set_size(hud_fs_range_badge, 160, 22);
    lv_obj_align(hud_fs_range_badge, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_set_style_bg_color(hud_fs_range_badge, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(hud_fs_range_badge, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(hud_fs_range_badge, 1, 0);
    lv_obj_set_style_radius(hud_fs_range_badge, 4, 0);
    lv_obj_clear_flag(hud_fs_range_badge, LV_OBJ_FLAG_SCROLLABLE);

    label_fs_min = lv_label_create(hud_fs_range_badge);
    lv_label_set_text(label_fs_min, "Min: --.-");
    lv_obj_align(label_fs_min, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_text_color(label_fs_min, UI_COLOR_CYAN, 0);
    lv_obj_set_style_text_font(label_fs_min, &lv_font_montserrat_10, 0);

    label_fs_max = lv_label_create(hud_fs_range_badge);
    lv_label_set_text(label_fs_max, "Max: --.- C");
    lv_obj_align(label_fs_max, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_text_color(label_fs_max, UI_COLOR_ORANGE, 0);
    lv_obj_set_style_text_font(label_fs_max, &lv_font_montserrat_10, 0);

    hud_fs_fps_badge = lv_obj_create(screen_thermal);
    lv_obj_set_size(hud_fs_fps_badge, 90, 22);
    lv_obj_align(hud_fs_fps_badge, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_set_style_bg_color(hud_fs_fps_badge, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(hud_fs_fps_badge, UI_COLOR_GREEN, 0);
    lv_obj_set_style_border_width(hud_fs_fps_badge, 1, 0);
    lv_obj_set_style_radius(hud_fs_fps_badge, 4, 0);
    lv_obj_clear_flag(hud_fs_fps_badge, LV_OBJ_FLAG_SCROLLABLE);

    label_fs_fps = lv_label_create(hud_fs_fps_badge);
    lv_label_set_text(label_fs_fps, "--.- BICUB");
    lv_obj_center(label_fs_fps);
    lv_obj_set_style_text_color(label_fs_fps, UI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(label_fs_fps, &lv_font_montserrat_10, 0);

    // Hide fullscreen overlays by default (visible only if viewport is 320x240)
    lv_obj_add_flag(hud_fs_center_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hud_fs_range_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hud_fs_fps_badge, LV_OBJ_FLAG_HIDDEN);
}

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
) {
    if (screen_thermal == nullptr) return;

    // Adjust thermal image widget positioning and dimension if changed
    if (lv_obj_get_x(img_thermal_feed) != vpX || lv_obj_get_width(img_thermal_feed) != vpW) {
        lv_obj_set_pos(img_thermal_feed, vpX, vpY);
        lv_obj_set_size(img_thermal_feed, vpW, vpH);
        thermal_img_dsc.header.w = vpW;
        thermal_img_dsc.header.h = vpH;
        thermal_img_dsc.data_size = vpW * vpH * sizeof(lv_color_t);
        lv_img_set_src(img_thermal_feed, &thermal_img_dsc);

        if (vpX > 0) {
            lv_obj_clear_flag(hud_left_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(hud_right_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(hud_fs_center_badge, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(hud_fs_range_badge, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(hud_fs_fps_badge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(hud_left_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(hud_right_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(hud_fs_center_badge, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(hud_fs_range_badge, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(hud_fs_fps_badge, LV_OBJ_FLAG_HIDDEN);
        }

        // Align reticle with the optical center of the thermal viewport
        int cx = vpX + vpW / 2;
        int cy = vpY + vpH / 2;
        lv_obj_set_pos(thermal_reticle_container, cx - 7, cy - 7);
    }

    const char* modeNames[5] = {"NEAR", "BILIN", "BICUB", "LANCZ", "TAAU"};
    const char* uSym = getUnitSym(unit);

    // Only update static colorbar canvas when palette actually changes
    static const uint16_t* lastRenderedPalette = nullptr;
    if (vpX > 0 && activePalette != nullptr && activePalette != lastRenderedPalette) {
        lastRenderedPalette = activePalette;
        for (int y = 0; y < 160; ++y) {
            int palIdx = 255 - (y * 255) / 159;
            lv_color_t c;
            c.full = activePalette[palIdx];
            for (int x = 0; x < 16; ++x) {
                colorbar_cbuf[y * 16 + x] = c;
            }
        }
        lv_obj_invalidate(canvas_colorbar);
    }

    // Throttle label text glyph re-rasterization to ~7 Hz (140 ms).
    // This leaves the 180x240 thermal feed as the ONLY dirty rectangle on the display
    // for ~5 out of every 6 frames, cutting SPI draw time from 30.7ms down to 17.2ms!
    static uint32_t lastHudTextUpdate = 0;
    uint32_t hudNow = millis();
    bool updateText = (hudNow - lastHudTextUpdate >= 140);

    if (updateText) {
        lastHudTextUpdate = hudNow;

        if (vpX > 0) {
            // Update Side Panels
            char buf[32];
            if (unit == TEMP_UNIT_KELVIN) {
                snprintf(buf, sizeof(buf), "%.1f", convertTemp(centerTemp, unit));
            } else {
                snprintf(buf, sizeof(buf), "%.1f" UI_SYMBOL_DEGREE, convertTemp(centerTemp, unit));
            }
            lv_label_set_text(label_center_temp, buf);

            lv_label_set_text(label_temp_unit, getUnitFullName(unit));

            snprintf(buf, sizeof(buf), "%.1f %s", convertTemp(minTemp, unit), uSym);
            lv_label_set_text(label_min_temp, buf);

            snprintf(buf, sizeof(buf), "%.1f %s", convertTemp(maxTemp, unit), uSym);
            lv_label_set_text(label_max_temp, buf);

            snprintf(buf, sizeof(buf), "%.1f FPS", fps);
            lv_label_set_text(label_fps, buf);

            lv_label_set_text(label_scaling_mode, modeNames[mode % 5]);

            if (unit == TEMP_UNIT_KELVIN) {
                snprintf(buf, sizeof(buf), "%.0f", convertTemp(maxTemp, unit));
            } else {
                snprintf(buf, sizeof(buf), "%.0f" UI_SYMBOL_DEGREE, convertTemp(maxTemp, unit));
            }
            lv_label_set_text(label_colorbar_max, buf);

            if (unit == TEMP_UNIT_KELVIN) {
                snprintf(buf, sizeof(buf), "%.0f", convertTemp(minTemp, unit));
            } else {
                snprintf(buf, sizeof(buf), "%.0f" UI_SYMBOL_DEGREE, convertTemp(minTemp, unit));
            }
            lv_label_set_text(label_colorbar_min, buf);

        } else {
            // Update Fullscreen Badges
            char buf[32];
            snprintf(buf, sizeof(buf), "+ %.1f %s", convertTemp(centerTemp, unit), uSym);
            lv_label_set_text(label_fs_center, buf);

            snprintf(buf, sizeof(buf), "Min:%.1f %s", convertTemp(minTemp, unit), uSym);
            lv_label_set_text(label_fs_min, buf);

            snprintf(buf, sizeof(buf), "Max:%.1f %s", convertTemp(maxTemp, unit), uSym);
            lv_label_set_text(label_fs_max, buf);

            snprintf(buf, sizeof(buf), "%.1f %s", fps, modeNames[mode % 5]);
            lv_label_set_text(label_fs_fps, buf);
        }
    }

    // Invalidate thermal image so LVGL renders the newly upscaled frame
    lv_obj_invalidate(img_thermal_feed);
}

// -------------------------------------------------------------
// SCREEN 3: MAIN MENU
// -------------------------------------------------------------
static void mainMenuBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        currentMenuIndex = idx;
        for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
            if (i == idx) {
                lv_obj_clear_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else if (code == LV_EVENT_CLICKED) {
        if (idx == 0) {
            uiNavToThermal();
        } else if (idx == 1) {
            uiNavToSettings();
        } else if (idx == 2) {
            // Power off action with smooth fade out
            Serial.println("[UI] Powering Off System...");
            enterDeepSleep();
        }
    }
}

static void buildMainMenuScreen() {
    screen_main_menu = lv_obj_create(NULL);
    lv_obj_add_style(screen_main_menu, &style_screen, 0);
    lv_obj_clear_flag(screen_main_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_main_menu, LV_SCROLLBAR_MODE_OFF);

    // Header Title
    lv_obj_t* header = lv_label_create(screen_main_menu);
    lv_label_set_text(header, "MAIN MENU");
    lv_obj_add_style(header, &style_header_title, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 14, 9);

    // Header Divider Line (0 to 319 fits inside 320-pixel display)
    static lv_point_t h_line_pts[] = {{0, 32}, {DISPLAY_WIDTH - 1, 32}};
    lv_obj_t* line_top = lv_line_create(screen_main_menu);
    lv_line_set_points(line_top, h_line_pts, 2);
    lv_obj_add_style(line_top, &style_divider, 0);

    // Menu Buttons Container
    int itemY[3] = {48, 100, 152};
    for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
        mainMenuBtns[i] = lv_btn_create(screen_main_menu);
        lv_obj_set_pos(mainMenuBtns[i], 30, itemY[i]);
        lv_obj_set_size(mainMenuBtns[i], 260, 40);
        lv_obj_add_style(mainMenuBtns[i], &style_btn_default, 0);
        lv_obj_add_style(mainMenuBtns[i], &style_btn_focused, LV_STATE_FOCUSED);
        lv_obj_add_style(mainMenuBtns[i], &style_btn_focused, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(mainMenuBtns[i], &style_btn_pressed, LV_STATE_PRESSED);

        // Selection Red Arrow
        mainMenuArrows[i] = lv_label_create(mainMenuBtns[i]);
        lv_label_set_text(mainMenuArrows[i], LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(mainMenuArrows[i], UI_COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(mainMenuArrows[i], &lv_font_montserrat_14, 0);
        lv_obj_align(mainMenuArrows[i], LV_ALIGN_LEFT_MID, 6, 0);
        if (i != 0) lv_obj_add_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);

        // Menu Label
        lv_obj_t* lbl = lv_label_create(mainMenuBtns[i]);
        lv_label_set_text(lbl, mainMenuLabels[i]);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 24, 0);

        lv_obj_add_event_cb(mainMenuBtns[i], mainMenuBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)i);
    }

    // Footer Divider Line (0 to 319 fits inside 320-pixel display)
    static lv_point_t f_line_pts[] = {{0, 216}, {DISPLAY_WIDTH - 1, 216}};
    lv_obj_t* line_bot = lv_line_create(screen_main_menu);
    lv_line_set_points(line_bot, f_line_pts, 2);
    lv_obj_add_style(line_bot, &style_divider, 0);

    // Footer Navigation Hints
    lv_obj_t* footer = lv_label_create(screen_main_menu);
    lv_label_set_text(footer, "[Rotate] Navigate   |   [Click] Select");
    lv_obj_add_style(footer, &style_footer_text, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 14, -8);
}

// -------------------------------------------------------------
// SCREEN 4: SETTINGS SCREEN
// -------------------------------------------------------------
static void settingsBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_obj_t* btn = lv_event_get_target(e);
        currentSettingsIndex = idx;
        for (int i = 0; i < SETTINGS_COUNT; ++i) {
            if (i == idx) {
                lv_obj_clear_flag(settingsArrows[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(settingsArrows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        // When the back option (item 5) is selected, its text must become the accent color
        if (idx == 5) {
            lv_obj_set_style_text_color(settingsNameLabels[5], UI_COLOR_ACCENT, 0);
        } else if (settingsNameLabels[5] != nullptr) {
            lv_obj_set_style_text_color(settingsNameLabels[5], UI_COLOR_TEXT, 0);
        }

        // Update 1-line description
        if (settings_desc_label != nullptr && idx < SETTINGS_COUNT) {
            lv_label_set_text(settings_desc_label, settingsDescriptions[idx]);
        }

        // Ensure focused item is smoothly scrolled into view
        lv_obj_scroll_to_view(btn, LV_ANIM_ON);

    } else if (code == LV_EVENT_DEFOCUSED) {
        if (idx == 5 && settingsNameLabels[5] != nullptr) {
            lv_obj_set_style_text_color(settingsNameLabels[5], UI_COLOR_TEXT_DIM, 0);
        }
    } else if (code == LV_EVENT_CLICKED) {
        switch (idx) {
            case 0: // Rotation: 0 -> 90 -> 180 -> 270
                currentRotationSetting = (currentRotationSetting + 1) % 4;
                updateCameraRotation(currentRotationSetting);
                break;
            case 1: // Temp Unit: Celsius -> Fahrenheit -> Kelvin -> Celsius
                currentTempUnitSetting = (TempUnit)((currentTempUnitSetting + 1) % 3);
                updateTempUnit(currentTempUnitSetting);
                break;
            case 2: // Color Palette: Ironbow -> White-Hot -> Rainbow
                currentPaletteSetting = (PaletteType)((currentPaletteSetting + 1) % 3);
                updatePalette(currentPaletteSetting);
                break;
            case 3: // Mirror Feed: OFF <-> ON
                currentMirrorSetting = !currentMirrorSetting;
                updateMirror(currentMirrorSetting);
                break;
            case 4: // Upscaling: Open dedicated subpage
                uiNavToScalingMenu();
                return;
            case 5: // Back to Main Menu
                uiNavToMainMenu();
                return;
        }

        // Animated value flash on change
        if (settingsValLabels[idx] != nullptr) {
            lv_anim_t a_val;
            lv_anim_init(&a_val);
            lv_anim_set_var(&a_val, settingsValLabels[idx]);
            lv_anim_set_values(&a_val, 255, 60);
            lv_anim_set_time(&a_val, 120);
            lv_anim_set_playback_time(&a_val, 120);
            lv_anim_set_exec_cb(&a_val, [](void* var, int32_t val) {
                lv_obj_set_style_text_opa((lv_obj_t*)var, val, 0);
            });
            lv_anim_start(&a_val);
        }
        updateSettingsValues();
    }
}

static void updateSettingsValues() {
    const char* rotStrs[4] = {"0" UI_SYMBOL_DEGREE, "90" UI_SYMBOL_DEGREE, "180" UI_SYMBOL_DEGREE, "270" UI_SYMBOL_DEGREE};
    const char* unitStrs[3] = {"Celsius (" UI_SYMBOL_DEGREE "C)", "Fahrenheit (" UI_SYMBOL_DEGREE "F)", "Kelvin (K)"};
    const char* palStrs[3] = {"Ironbow", "White-Hot", "Rainbow"};
    const char* mirStrs[2] = {"OFF", "ON"};
    const char* scaleStrs[5] = {"Nearest", "Bilinear", "Bicubic", "Lanczos", "TAAU"};

    lv_label_set_text(settingsValLabels[0], rotStrs[currentRotationSetting % 4]);
    lv_label_set_text(settingsValLabels[1], unitStrs[currentTempUnitSetting % 3]);
    lv_label_set_text(settingsValLabels[2], palStrs[currentPaletteSetting % 3]);
    lv_label_set_text(settingsValLabels[3], mirStrs[currentMirrorSetting ? 1 : 0]);
    lv_label_set_text(settingsValLabels[4], scaleStrs[currentScalingSetting % 5]);
}

static void buildSettingsScreen() {
    screen_settings = lv_obj_create(NULL);
    lv_obj_add_style(screen_settings, &style_screen, 0);
    lv_obj_clear_flag(screen_settings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_settings, LV_SCROLLBAR_MODE_OFF);

    // Header Title
    lv_obj_t* header = lv_label_create(screen_settings);
    lv_label_set_text(header, "SETTINGS");
    lv_obj_add_style(header, &style_header_title, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 14, 9);

    // Header Divider Line
    static lv_point_t h_line_pts[] = {{0, 32}, {DISPLAY_WIDTH - 1, 32}};
    lv_obj_t* line_top = lv_line_create(screen_settings);
    lv_line_set_points(line_top, h_line_pts, 2);
    lv_obj_add_style(line_top, &style_divider, 0);

    // Scrollable Settings Items Container (with clean vertical scrollbar)
    settings_list = lv_obj_create(screen_settings);
    lv_obj_set_pos(settings_list, 10, 35);
    lv_obj_set_size(settings_list, 296, 152);
    lv_obj_set_style_bg_opa(settings_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings_list, 0, 0);
    lv_obj_set_style_pad_all(settings_list, 0, 0);
    lv_obj_set_scroll_dir(settings_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(settings_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(settings_list, UI_COLOR_FOCUS_GREY, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(settings_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(settings_list, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(settings_list, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(settings_list, 1, LV_PART_SCROLLBAR);

    int itemH = 28;
    int spacing = 6;
    for (int i = 0; i < SETTINGS_COUNT; ++i) {
        settingsBtns[i] = lv_btn_create(settings_list);
        lv_obj_set_pos(settingsBtns[i], 4, i * (itemH + spacing));
        lv_obj_set_size(settingsBtns[i], 282, itemH);
        lv_obj_add_style(settingsBtns[i], &style_btn_default, 0);
        if (i == 5) {
            // Back item: filled with dark grey and accent border when focused
            lv_obj_add_style(settingsBtns[i], &style_btn_back_focused, LV_STATE_FOCUSED);
            lv_obj_add_style(settingsBtns[i], &style_btn_back_focused, LV_STATE_FOCUS_KEY);
        } else {
            lv_obj_add_style(settingsBtns[i], &style_btn_focused, LV_STATE_FOCUSED);
            lv_obj_add_style(settingsBtns[i], &style_btn_focused, LV_STATE_FOCUS_KEY);
        }
        lv_obj_add_style(settingsBtns[i], &style_btn_pressed, LV_STATE_PRESSED);

        // Arrow indicator
        settingsArrows[i] = lv_label_create(settingsBtns[i]);
        if (i == 5) {
            lv_label_set_text(settingsArrows[i], LV_SYMBOL_LEFT);
        } else {
            lv_label_set_text(settingsArrows[i], LV_SYMBOL_RIGHT);
        }
        lv_obj_set_style_text_color(settingsArrows[i], UI_COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(settingsArrows[i], &lv_font_montserrat_12, 0);
        lv_obj_align(settingsArrows[i], LV_ALIGN_LEFT_MID, 4, 0);
        if (i != 0) lv_obj_add_flag(settingsArrows[i], LV_OBJ_FLAG_HIDDEN);

        // Setting Name
        settingsNameLabels[i] = lv_label_create(settingsBtns[i]);
        lv_label_set_text(settingsNameLabels[i], settingsLabels[i]);
        lv_obj_align(settingsNameLabels[i], LV_ALIGN_LEFT_MID, 18, 0);
        lv_obj_set_style_text_font(settingsNameLabels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(settingsNameLabels[i], (i == 5) ? UI_COLOR_TEXT_DIM : UI_COLOR_TEXT, 0);

        // Setting Value (Right column)
        if (i < 5) {
            settingsValLabels[i] = lv_label_create(settingsBtns[i]);
            lv_obj_align(settingsValLabels[i], LV_ALIGN_RIGHT_MID, -6, 0);
            lv_obj_set_style_text_color(settingsValLabels[i], UI_COLOR_ACCENT, 0);
            lv_obj_set_style_text_font(settingsValLabels[i], &lv_font_montserrat_12, 0);
        } else {
            settingsValLabels[i] = nullptr;
        }

        lv_obj_add_event_cb(settingsBtns[i], settingsBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)i);
    }
    updateSettingsValues();

    // Divider Line above description
    static lv_point_t desc_line_pts[] = {{0, 192}, {DISPLAY_WIDTH - 1, 192}};
    lv_obj_t* line_mid = lv_line_create(screen_settings);
    lv_line_set_points(line_mid, desc_line_pts, 2);
    lv_obj_add_style(line_mid, &style_divider, 0);

    // 1-line contextual description label
    settings_desc_label = lv_label_create(screen_settings);
    lv_obj_set_pos(settings_desc_label, 14, 197);
    lv_obj_set_size(settings_desc_label, 292, 14);
    lv_obj_set_style_text_color(settings_desc_label, UI_COLOR_YELLOW, 0);
    lv_obj_set_style_text_font(settings_desc_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(settings_desc_label, settingsDescriptions[0]);

    // Footer Divider Line
    static lv_point_t f_line_pts[] = {{0, 216}, {DISPLAY_WIDTH - 1, 216}};
    lv_obj_t* line_bot = lv_line_create(screen_settings);
    lv_line_set_points(line_bot, f_line_pts, 2);
    lv_obj_add_style(line_bot, &style_divider, 0);

    // Footer Navigation Hints
    lv_obj_t* footer = lv_label_create(screen_settings);
    lv_label_set_text(footer, "[Rotate] Navigate   |   [Click] Select / Adjust");
    lv_obj_add_style(footer, &style_footer_text, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 14, -8);
}

// -------------------------------------------------------------
// SCREEN 4B: SCALING OPTIONS SCREEN
// -------------------------------------------------------------
static void scalingBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_obj_t* btn = lv_event_get_target(e);
        currentScalingMenuIndex = idx;
        for (int i = 0; i < SCALING_OPTIONS_COUNT; ++i) {
            if (i == idx) {
                lv_obj_clear_flag(scalingArrows[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(scalingArrows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (idx == 5) {
            lv_obj_set_style_text_color(scalingNameLabels[5], UI_COLOR_ACCENT, 0);
        } else if (scalingNameLabels[5] != nullptr) {
            lv_obj_set_style_text_color(scalingNameLabels[5], UI_COLOR_TEXT, 0);
        }

        if (scaling_desc_label != nullptr && idx < SCALING_OPTIONS_COUNT) {
            lv_label_set_text(scaling_desc_label, scalingOptionDescs[idx]);
        }

        lv_obj_scroll_to_view(btn, LV_ANIM_ON);

    } else if (code == LV_EVENT_DEFOCUSED) {
        if (idx == 5 && scalingNameLabels[5] != nullptr) {
            lv_obj_set_style_text_color(scalingNameLabels[5], UI_COLOR_TEXT_DIM, 0);
        }
    } else if (code == LV_EVENT_CLICKED) {
        if (idx < 5) {
            currentScalingSetting = (ScalingMode)idx;
            updateScalingMode(currentScalingSetting);
            updateScalingValues();
            updateSettingsValues();

            // Flash active tag animation
            if (scalingStatusLabels[idx] != nullptr) {
                lv_anim_t a_val;
                lv_anim_init(&a_val);
                lv_anim_set_var(&a_val, scalingStatusLabels[idx]);
                lv_anim_set_values(&a_val, 255, 60);
                lv_anim_set_time(&a_val, 120);
                lv_anim_set_playback_time(&a_val, 120);
                lv_anim_set_exec_cb(&a_val, [](void* var, int32_t val) {
                    lv_obj_set_style_text_opa((lv_obj_t*)var, val, 0);
                });
                lv_anim_start(&a_val);
            }
        } else {
            // Back to Settings screen (focused on item 4 "Upscaling")
            uiNavToSettings(4);
        }
    }
}

static void updateScalingValues() {
    for (int i = 0; i < 5; ++i) {
        if (scalingStatusLabels[i] != nullptr) {
            if ((ScalingMode)i == currentScalingSetting) {
                lv_label_set_text(scalingStatusLabels[i], "[ACTIVE]");
                lv_obj_set_style_text_color(scalingStatusLabels[i], UI_COLOR_GREEN, 0);
            } else {
                lv_label_set_text(scalingStatusLabels[i], "");
            }
        }
    }
}

static void buildScalingScreen() {
    screen_scaling = lv_obj_create(NULL);
    lv_obj_add_style(screen_scaling, &style_screen, 0);
    lv_obj_clear_flag(screen_scaling, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_scaling, LV_SCROLLBAR_MODE_OFF);

    // Header Title
    lv_obj_t* header = lv_label_create(screen_scaling);
    lv_label_set_text(header, "UPSCALING MODES");
    lv_obj_add_style(header, &style_header_title, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 14, 9);

    // Header Divider Line
    static lv_point_t h_line_pts[] = {{0, 32}, {DISPLAY_WIDTH - 1, 32}};
    lv_obj_t* line_top = lv_line_create(screen_scaling);
    lv_line_set_points(line_top, h_line_pts, 2);
    lv_obj_add_style(line_top, &style_divider, 0);

    // Scrollable list
    scaling_list = lv_obj_create(screen_scaling);
    lv_obj_set_pos(scaling_list, 10, 35);
    lv_obj_set_size(scaling_list, 296, 152);
    lv_obj_set_style_bg_opa(scaling_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scaling_list, 0, 0);
    lv_obj_set_style_pad_all(scaling_list, 0, 0);
    lv_obj_set_scroll_dir(scaling_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scaling_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(scaling_list, UI_COLOR_FOCUS_GREY, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(scaling_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(scaling_list, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(scaling_list, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(scaling_list, 1, LV_PART_SCROLLBAR);

    int itemH = 28;
    int spacing = 6;
    for (int i = 0; i < SCALING_OPTIONS_COUNT; ++i) {
        scalingBtns[i] = lv_btn_create(scaling_list);
        lv_obj_set_pos(scalingBtns[i], 4, i * (itemH + spacing));
        lv_obj_set_size(scalingBtns[i], 282, itemH);
        lv_obj_add_style(scalingBtns[i], &style_btn_default, 0);
        if (i == 5) {
            lv_obj_add_style(scalingBtns[i], &style_btn_back_focused, LV_STATE_FOCUSED);
            lv_obj_add_style(scalingBtns[i], &style_btn_back_focused, LV_STATE_FOCUS_KEY);
        } else {
            lv_obj_add_style(scalingBtns[i], &style_btn_focused, LV_STATE_FOCUSED);
            lv_obj_add_style(scalingBtns[i], &style_btn_focused, LV_STATE_FOCUS_KEY);
        }
        lv_obj_add_style(scalingBtns[i], &style_btn_pressed, LV_STATE_PRESSED);

        // Arrow
        scalingArrows[i] = lv_label_create(scalingBtns[i]);
        if (i == 5) {
            lv_label_set_text(scalingArrows[i], LV_SYMBOL_LEFT);
        } else {
            lv_label_set_text(scalingArrows[i], LV_SYMBOL_RIGHT);
        }
        lv_obj_set_style_text_color(scalingArrows[i], UI_COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(scalingArrows[i], &lv_font_montserrat_12, 0);
        lv_obj_align(scalingArrows[i], LV_ALIGN_LEFT_MID, 4, 0);
        if (i != 0) lv_obj_add_flag(scalingArrows[i], LV_OBJ_FLAG_HIDDEN);

        // Name
        scalingNameLabels[i] = lv_label_create(scalingBtns[i]);
        lv_label_set_text(scalingNameLabels[i], scalingOptionLabels[i]);
        lv_obj_align(scalingNameLabels[i], LV_ALIGN_LEFT_MID, 18, 0);
        lv_obj_set_style_text_font(scalingNameLabels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(scalingNameLabels[i], (i == 5) ? UI_COLOR_TEXT_DIM : UI_COLOR_TEXT, 0);

        // Status indicator (Right column)
        if (i < 5) {
            scalingStatusLabels[i] = lv_label_create(scalingBtns[i]);
            lv_obj_align(scalingStatusLabels[i], LV_ALIGN_RIGHT_MID, -6, 0);
            lv_obj_set_style_text_color(scalingStatusLabels[i], UI_COLOR_GREEN, 0);
            lv_obj_set_style_text_font(scalingStatusLabels[i], &lv_font_montserrat_10, 0);
        } else {
            scalingStatusLabels[i] = nullptr;
        }

        lv_obj_add_event_cb(scalingBtns[i], scalingBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)i);
    }
    updateScalingValues();

    // Divider Line above description
    static lv_point_t desc_line_pts[] = {{0, 192}, {DISPLAY_WIDTH - 1, 192}};
    lv_obj_t* line_mid = lv_line_create(screen_scaling);
    lv_line_set_points(line_mid, desc_line_pts, 2);
    lv_obj_add_style(line_mid, &style_divider, 0);

    // 1-line description label
    scaling_desc_label = lv_label_create(screen_scaling);
    lv_obj_set_pos(scaling_desc_label, 14, 197);
    lv_obj_set_size(scaling_desc_label, 292, 14);
    lv_obj_set_style_text_color(scaling_desc_label, UI_COLOR_YELLOW, 0);
    lv_obj_set_style_text_font(scaling_desc_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(scaling_desc_label, scalingOptionDescs[0]);

    // Footer Divider Line
    static lv_point_t f_line_pts[] = {{0, 216}, {DISPLAY_WIDTH - 1, 216}};
    lv_obj_t* line_bot = lv_line_create(screen_scaling);
    lv_line_set_points(line_bot, f_line_pts, 2);
    lv_obj_add_style(line_bot, &style_divider, 0);

    // Footer Navigation Hints
    lv_obj_t* footer = lv_label_create(screen_scaling);
    lv_label_set_text(footer, "[Rotate] Navigate   |   [Click] Select / Back");
    lv_obj_add_style(footer, &style_footer_text, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 14, -8);
}

// -------------------------------------------------------------
// SCREEN 5: SENSOR ERROR SCREEN
// -------------------------------------------------------------
static void buildErrorScreen() {
    screen_error = lv_obj_create(NULL);
    lv_obj_add_style(screen_error, &style_screen, 0);
    lv_obj_clear_flag(screen_error, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_error, LV_SCROLLBAR_MODE_OFF);

    // Red Warning Outline
    lv_obj_t* border_box = lv_obj_create(screen_error);
    lv_obj_set_size(border_box, DISPLAY_WIDTH - 12, DISPLAY_HEIGHT - 12);
    lv_obj_center(border_box);
    lv_obj_set_style_bg_opa(border_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(border_box, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(border_box, 2, 0);
    lv_obj_set_style_radius(border_box, 4, 0);

    // Red Title Banner
    lv_obj_t* banner = lv_obj_create(screen_error);
    lv_obj_set_size(banner, DISPLAY_WIDTH - 24, 26);
    lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(banner, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(banner, 2, 0);

    lv_obj_t* banner_title = lv_label_create(banner);
    lv_label_set_text(banner_title, "THERMAL SENSOR ERROR");
    lv_obj_center(banner_title);
    lv_obj_set_style_text_color(banner_title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(banner_title, &lv_font_montserrat_14, 0);

    // Subtitle
    lv_obj_t* sub = lv_label_create(screen_error);
    lv_label_set_text(sub, "MLX90640 not responding on I2C bus (Addr 0x33)");
    lv_obj_set_style_text_color(sub, UI_COLOR_YELLOW, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 18, 44);

    // Pin Checklist
    lv_obj_t* pins = lv_label_create(screen_error);
    char pinBuf[128];
    snprintf(pinBuf, sizeof(pinBuf),
             "CHECK CONNECTIONS:\n  * SDA -> GPIO %d\n  * SCL -> GPIO %d\n  * VIN -> 3.3V (NOT 5V)\n  * GND -> Common GND",
             PIN_I2C_SDA, PIN_I2C_SCL);
    lv_label_set_text(pins, pinBuf);
    lv_obj_set_style_text_color(pins, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(pins, &lv_font_montserrat_10, 0);
    lv_obj_align(pins, LV_ALIGN_TOP_LEFT, 18, 62);

    // Auto Reconnect Label
    error_attempt_label = lv_label_create(screen_error);
    lv_label_set_text(error_attempt_label, "Auto-reconnecting... (Attempt #1)");
    lv_obj_set_style_text_color(error_attempt_label, UI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(error_attempt_label, &lv_font_montserrat_10, 0);
    lv_obj_align(error_attempt_label, LV_ALIGN_TOP_LEFT, 18, 140);
}

// -------------------------------------------------------------
// NAVIGATION TRANSITIONS WITH ANIMATIONS
// -------------------------------------------------------------
void uiNavToThermal() {
    currentAppState = APP_STATE_THERMAL_VIEW;
    lv_group_remove_all_objs(ui_encoder_group);
    EncoderEventType dummy;
    while (getEncoderEvent(dummy, 0)) { }

    // Quick crossfade (100 ms) between thermal feed and main menu
    lv_scr_load_anim(screen_thermal, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, false);
    Serial.println("[UI] Transitioned to Live Thermal View (100ms Crossfade)");
}

void uiNavToMainMenu() {
    AppState prevState = currentAppState;
    currentAppState = APP_STATE_MAIN_MENU;
    lv_group_remove_all_objs(ui_encoder_group);
    for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
        lv_group_add_obj(ui_encoder_group, mainMenuBtns[i]);
    }
    currentMenuIndex = 0;
    for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
        if (i == 0) {
            lv_obj_clear_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_group_focus_obj(mainMenuBtns[0]);

    EncoderEventType dummy;
    while (getEncoderEvent(dummy, 0)) { }

    if (prevState == APP_STATE_SETTINGS || prevState == APP_STATE_SCALING_MENU) {
        // Between menu screens: old slide transition sped up by 100% (200ms -> 100ms)
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
    lv_group_remove_all_objs(ui_encoder_group);
    for (int i = 0; i < SETTINGS_COUNT; ++i) {
        lv_group_add_obj(ui_encoder_group, settingsBtns[i]);
    }
    if (focusIndex < 0 || focusIndex >= SETTINGS_COUNT) focusIndex = 0;
    currentSettingsIndex = focusIndex;
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

    EncoderEventType dummy;
    while (getEncoderEvent(dummy, 0)) { }

    if (prevState == APP_STATE_SCALING_MENU) {
        // Subpage back to settings: slide right
        lv_scr_load_anim(screen_settings, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, false);
    } else {
        // Main menu to settings: slide left
        lv_scr_load_anim(screen_settings, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);
    }
    Serial.println("[UI] Transitioned to Settings (100ms Slide)");
}

void uiNavToScalingMenu() {
    currentAppState = APP_STATE_SCALING_MENU;
    lv_group_remove_all_objs(ui_encoder_group);
    for (int i = 0; i < SCALING_OPTIONS_COUNT; ++i) {
        lv_group_add_obj(ui_encoder_group, scalingBtns[i]);
    }
    int initialFocus = (int)currentScalingSetting;
    if (initialFocus < 0 || initialFocus >= 5) initialFocus = 0;
    currentScalingMenuIndex = initialFocus;
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

    EncoderEventType dummy;
    while (getEncoderEvent(dummy, 0)) { }

    // Slide left into subpage
    lv_scr_load_anim(screen_scaling, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);
    Serial.println("[UI] Transitioned to Scaling Menu (100ms Slide)");
}

void uiNavToSensorError(uint32_t attemptCount) {
    currentAppState = APP_STATE_SENSOR_ERROR;
    char buf[64];
    snprintf(buf, sizeof(buf), "Auto-reconnecting... (Attempt #%u)", attemptCount);
    if (error_attempt_label) lv_label_set_text(error_attempt_label, buf);
    lv_scr_load_anim(screen_error, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, false);
}

void uiDismissSensorError() {
    uiNavToThermal();
}

// -------------------------------------------------------------
// ENCODER INPUT PROCESSING
// -------------------------------------------------------------
bool processUIInput(EncoderEventType event) {
    if (event == ENCODER_EVENT_NONE) return false;

    if (currentAppState == APP_STATE_THERMAL_VIEW) {
        if (event == ENCODER_EVENT_CLICK) {
            uiNavToMainMenu();
            return true;
        }
    }
    return false;
}
