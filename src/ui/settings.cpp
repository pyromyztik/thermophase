#include "ui.hpp"
#include "display.hpp"

// Settings Shared Variables
lv_obj_t* settingsBtns[SETTINGS_COUNT];
lv_obj_t* settingsArrows[SETTINGS_COUNT];
lv_obj_t* settings_desc_label = nullptr;
lv_obj_t* settings_list = nullptr;

const char* settingsDescriptions[] = {
    "Adjust display backlight brightness",
    "Adjust sensor orientation (0" UI_SYMBOL_DEGREE ", 90" UI_SYMBOL_DEGREE ", 180" UI_SYMBOL_DEGREE ", 270" UI_SYMBOL_DEGREE ")",
    "Select temperature unit (" UI_SYMBOL_DEGREE "C, " UI_SYMBOL_DEGREE "F, K)",
    "Select false-color palette for thermal gradients",
    "Horizontally flip thermal video stream",
    "Open upscaler menu to configure interpolation modes",
    "Return to main menu"
};

static const char* settingsLabels[] = {
    "Brightness",
    "Rotation",
    "Temp Unit",
    "Color Palette",
    "Mirror Feed",
    "Upscaling",
    "Back to Main Menu"
};

static lv_obj_t* settingsNameLabels[SETTINGS_COUNT];
static lv_obj_t* settingsValLabels[SETTINGS_COUNT];

static void settingsBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_obj_t* btn = lv_event_get_target(e);
        for (int i = 0; i < SETTINGS_COUNT; ++i) {
            if (i == idx) {
                lv_obj_clear_flag(settingsArrows[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(settingsArrows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        // When the back option (item 6) is selected, its text must become the accent color
        if (idx == 6) {
            lv_obj_set_style_text_color(settingsNameLabels[6], UI_COLOR_ACCENT, 0);
        } else if (settingsNameLabels[6] != nullptr) {
            lv_obj_set_style_text_color(settingsNameLabels[6], UI_COLOR_TEXT, 0);
        }

        // Update 1-line description
        if (settings_desc_label != nullptr && idx < SETTINGS_COUNT) {
            lv_label_set_text(settings_desc_label, settingsDescriptions[idx]);
        }

        // Ensure focused item is smoothly scrolled into view
        lv_obj_scroll_to_view(btn, LV_ANIM_ON);

    } else if (code == LV_EVENT_DEFOCUSED) {
        if (idx == 6 && settingsNameLabels[6] != nullptr) {
            lv_obj_set_style_text_color(settingsNameLabels[6], UI_COLOR_TEXT_DIM, 0);
        }
    } else if (code == LV_EVENT_CLICKED) {
        switch (idx) {
            case 0: // Brightness: Open dedicated slider subpage
                uiNavToBrightness();
                return;
            case 1: { // Rotation: 0 -> 90 -> 180 -> 270
                int nextRot = (getCameraRotationSetting() + 1) % 4;
                setCameraRotationSetting(nextRot);
                updateCameraRotation(nextRot);
                break;
            }
            case 2: { // Temp Unit: Celsius -> Fahrenheit -> Kelvin -> Celsius
                TempUnit nextUnit = (TempUnit)((getTempUnitSetting() + 1) % 3);
                setTempUnitSetting(nextUnit);
                updateTempUnit(nextUnit);
                break;
            }
            case 3: { // Color Palette: Ironbow -> White-Hot -> Rainbow
                PaletteType nextPal = (PaletteType)((getPaletteSetting() + 1) % 3);
                setPaletteSetting(nextPal);
                updatePalette(nextPal);
                break;
            }
            case 4: { // Mirror Feed: OFF <-> ON
                bool nextMirror = !getMirrorSetting();
                setMirrorSetting(nextMirror);
                updateMirror(nextMirror);
                break;
            }
            case 5: // Upscaling: Open dedicated subpage
                uiNavToScalingMenu();
                return;
            case 6: // Back to Main Menu
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

void updateSettingsValues() {
    char brightBuf[16];
    snprintf(brightBuf, sizeof(brightBuf), "%d%%", getDisplayBrightnessSetting());
    if (settingsValLabels[0]) lv_label_set_text(settingsValLabels[0], brightBuf);

    const char* rotStrs[4] = {"0" UI_SYMBOL_DEGREE, "90" UI_SYMBOL_DEGREE, "180" UI_SYMBOL_DEGREE, "270" UI_SYMBOL_DEGREE};
    const char* unitStrs[3] = {"Celsius (" UI_SYMBOL_DEGREE "C)", "Fahrenheit (" UI_SYMBOL_DEGREE "F)", "Kelvin (K)"};
    const char* palStrs[3] = {"Ironbow", "White-Hot", "Rainbow"};
    const char* mirStrs[2] = {"OFF", "ON"};
    const char* scaleStrs[5] = {"Nearest", "Bilinear", "Bicubic", "Lanczos", "TAAU"};

    if (settingsValLabels[1]) lv_label_set_text(settingsValLabels[1], rotStrs[getCameraRotationSetting() % 4]);
    if (settingsValLabels[2]) lv_label_set_text(settingsValLabels[2], unitStrs[getTempUnitSetting() % 3]);
    if (settingsValLabels[3]) lv_label_set_text(settingsValLabels[3], palStrs[getPaletteSetting() % 3]);
    if (settingsValLabels[4]) lv_label_set_text(settingsValLabels[4], mirStrs[getMirrorSetting() ? 1 : 0]);
    if (settingsValLabels[5]) lv_label_set_text(settingsValLabels[5], scaleStrs[getScalingModeSetting() % 5]);
}

void buildSettingsScreen() {
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

    // Vertical Divider Line separating options from right-hand contextual hints
    static lv_point_t v_line_pts[] = {{216, 32}, {216, 216}};
    lv_obj_t* line_vert = lv_line_create(screen_settings);
    lv_line_set_points(line_vert, v_line_pts, 2);
    lv_obj_add_style(line_vert, &style_divider, 0);

    // Scrollable Settings Items Container (Left side)
    settings_list = lv_obj_create(screen_settings);
    lv_obj_set_pos(settings_list, 4, 34);
    lv_obj_set_size(settings_list, 208, 180);
    lv_obj_set_style_bg_opa(settings_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings_list, 0, 0);
    lv_obj_set_style_pad_all(settings_list, 0, 0);
    lv_obj_set_scroll_dir(settings_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(settings_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(settings_list, UI_COLOR_FOCUS_GREY, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(settings_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(settings_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(settings_list, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(settings_list, 1, LV_PART_SCROLLBAR);

    int itemH = 26;
    int spacing = 4;
    for (int i = 0; i < SETTINGS_COUNT; ++i) {
        settingsBtns[i] = lv_btn_create(settings_list);
        lv_obj_set_pos(settingsBtns[i], 2, i * (itemH + spacing));
        lv_obj_set_size(settingsBtns[i], 198, itemH);
        lv_obj_add_style(settingsBtns[i], &style_btn_default, 0);
        if (i == 6) {
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
        if (i == 6) {
            lv_label_set_text(settingsArrows[i], LV_SYMBOL_LEFT);
        } else {
            lv_label_set_text(settingsArrows[i], LV_SYMBOL_RIGHT);
        }
        lv_obj_set_style_text_color(settingsArrows[i], UI_COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(settingsArrows[i], &lv_font_montserrat_12, 0);
        lv_obj_align(settingsArrows[i], LV_ALIGN_LEFT_MID, 0, 0);
        if (i != 0) lv_obj_add_flag(settingsArrows[i], LV_OBJ_FLAG_HIDDEN);

        // Setting Name
        settingsNameLabels[i] = lv_label_create(settingsBtns[i]);
        lv_label_set_text(settingsNameLabels[i], settingsLabels[i]);
        lv_obj_align(settingsNameLabels[i], LV_ALIGN_LEFT_MID, 13, 0);
        lv_obj_set_style_text_font(settingsNameLabels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(settingsNameLabels[i], (i == 6) ? UI_COLOR_TEXT_DIM : UI_COLOR_TEXT, 0);

        // Setting Value (Right column)
        if (i < 6) {
            settingsValLabels[i] = lv_label_create(settingsBtns[i]);
            lv_obj_align(settingsValLabels[i], LV_ALIGN_RIGHT_MID, -3, 0);
            lv_obj_set_style_text_color(settingsValLabels[i], UI_COLOR_ACCENT, 0);
            lv_obj_set_style_text_font(settingsValLabels[i], &lv_font_montserrat_12, 0);
        } else {
            settingsValLabels[i] = nullptr;
        }

        lv_obj_add_event_cb(settingsBtns[i], settingsBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)i);
    }
    updateSettingsValues();

    // Right-Side Contextual Description Panel
    lv_obj_t* hint_hdr = lv_label_create(screen_settings);
    lv_label_set_text(hint_hdr, "INFO");
    lv_obj_set_pos(hint_hdr, 222, 38);
    lv_obj_set_style_text_color(hint_hdr, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint_hdr, &lv_font_montserrat_10, 0);

    settings_desc_label = lv_label_create(screen_settings);
    lv_obj_set_pos(settings_desc_label, 222, 54);
    lv_obj_set_size(settings_desc_label, 92, 150);
    lv_label_set_long_mode(settings_desc_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(settings_desc_label, UI_COLOR_TEXT_DIM, 0);
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
