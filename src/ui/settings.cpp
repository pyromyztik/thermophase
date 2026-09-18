#include "ui.hpp"
#include "display.hpp"
#include "storage.hpp"
#include "ble.hpp"

// Settings Shared Variables
lv_obj_t* settingsBtns[SETTINGS_COUNT];
lv_obj_t* settingsArrows[SETTINGS_COUNT];
lv_obj_t* settings_desc_label = nullptr;
lv_obj_t* settings_list = nullptr;

const char* settingsDescriptions[SETTINGS_COUNT] = {
    "Adjust display backlight brightness",
    "Rotate display 180" UI_SYMBOL_DEGREE " upside down for alternative mounting",
    "Horizontally flip thermal video stream",
    "Select false-color palette for thermal gradients",
    "Open upscaler menu to configure interpolation modes and live filtering",
    "Select temperature unit (" UI_SYMBOL_DEGREE "C, " UI_SYMBOL_DEGREE "F, K)",
    "Select Thermal Sensor's I2C clock: 400 kHz (clean & stable - runs at 8fps) or 800 kHz (little more noisy, but runs at 14-15fps)",
    "Enable or disable the on-board WS2812 RGB target / status indicator LED",
    "Enable or disable Bluetooth wireless telemetry & remote command streaming",
    "Save all settings and return to main menu."
};

static const char* settingsLabels[SETTINGS_COUNT] = {
    "Brightness",
    "Orientation",
    "Mirror Feed",
    "Color Palette",
    "Upscaling",
    "Temp Unit",
    "I2C Clock",
    "RGB LED",
    "Bluetooth",
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
        // When the back option (last item) is selected, its text and arrow become black on focus
        if (idx == SETTINGS_COUNT - 1) {
            lv_obj_set_style_text_color(settingsNameLabels[SETTINGS_COUNT - 1], lv_color_black(), 0);
            if (settingsArrows[SETTINGS_COUNT - 1] != nullptr) {
                lv_obj_set_style_text_color(settingsArrows[SETTINGS_COUNT - 1], lv_color_black(), 0);
            }
        } else if (settingsNameLabels[SETTINGS_COUNT - 1] != nullptr) {
            lv_obj_set_style_text_color(settingsNameLabels[SETTINGS_COUNT - 1], UI_COLOR_TEXT_DIM, 0);
        }

        // Update 1-line description (dynamic info for Bluetooth)
        if (settings_desc_label != nullptr && idx < SETTINGS_COUNT) {
            if (idx == 8) {
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
                lv_label_set_text(settings_desc_label, settingsDescriptions[idx]);
            }
        }

        // Ensure focused item is smoothly scrolled into view
        if (idx == 0) {
            lv_obj_scroll_to_y(settings_list, 0, LV_ANIM_ON);
        } else {
            lv_obj_scroll_to_view(btn, LV_ANIM_ON);
        }

    } else if (code == LV_EVENT_DEFOCUSED) {
        if (idx == SETTINGS_COUNT - 1 && settingsNameLabels[SETTINGS_COUNT - 1] != nullptr) {
            lv_obj_set_style_text_color(settingsNameLabels[SETTINGS_COUNT - 1], UI_COLOR_TEXT_DIM, 0);
        }
    } else if (code == LV_EVENT_CLICKED) {
        switch (idx) {
            case 0: // Brightness: Open dedicated slider subpage
                uiNavToBrightness();
                return;
            case 1: { // Orientation: Normal <-> 180 deg
                uint8_t nextRot = (getDisplayRotationSetting() == 1) ? 0 : 1;
                setDisplayRotationSetting(nextRot);
                break;
            }
            case 2: { // Mirror Feed: OFF <-> ON
                bool nextMirror = !getMirrorSetting();
                setMirrorSetting(nextMirror);
                updateMirror(nextMirror);
                break;
            }
            case 3: { // Color Palette: Ironbow -> White-Hot -> Rainbow
                PaletteType nextPal = (PaletteType)((getPaletteSetting() + 1) % 3);
                setPaletteSetting(nextPal);
                updatePalette(nextPal);
                break;
            }
            case 4: // Upscaling: Open dedicated subpage
                uiNavToScalingMenu();
                return;
            case 5: { // Temp Unit: Celsius -> Fahrenheit -> Kelvin -> Celsius
                TempUnit nextUnit = (TempUnit)((getTempUnitSetting() + 1) % 3);
                setTempUnitSetting(nextUnit);
                updateTempUnit(nextUnit);
                break;
            }
            case 6: { // I2C Clock: 400 kHz <-> 800 kHz
                uint32_t currentSpeed = getI2CSpeedSetting();
                uint32_t nextSpeed = (currentSpeed == I2C_SPEED_800K) ? I2C_SPEED_400K : I2C_SPEED_800K;
                setI2CSpeedSetting(nextSpeed);
                break;
            }
            case 7: { // RGB LED: OFF <-> ON
                bool nextRgb = !getRgbLedEnabledSetting();
                setRgbLedEnabledSetting(nextRgb);
                break;
            }
            case 8: { // Bluetooth: OFF <-> ON
                bool nextBle = !getBLEEnabledSetting();
                setBLEEnabledSetting(nextBle);
                if (settings_desc_label != nullptr) {
                    char bleDesc[140];
                    if (!nextBle) {
                        snprintf(bleDesc, sizeof(bleDesc), "Bluetooth is OFF (saves ~35-45 mA / ~12%% battery). Click to enable wireless telemetry.");
                    } else {
                        snprintf(bleDesc, sizeof(bleDesc), "Bluetooth ON: Advertising as 'thermophase'. Streams live telemetry to Web Terminal.");
                    }
                    lv_label_set_text(settings_desc_label, bleDesc);
                }
                break;
            }
            case 9: // Back to Main Menu
                saveSettingsIfDirty();
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

    if (settingsValLabels[1]) {
        lv_label_set_text(settingsValLabels[1], (getDisplayRotationSetting() == 1) ? "180" UI_SYMBOL_DEGREE : "0" UI_SYMBOL_DEGREE);
    }

    const char* mirStrs[2] = {"OFF", "ON"};
    if (settingsValLabels[2]) lv_label_set_text(settingsValLabels[2], mirStrs[getMirrorSetting() ? 1 : 0]);

    const char* palStrs[3] = {"Ironbow", "White-Hot", "Rainbow"};
    if (settingsValLabels[3]) lv_label_set_text(settingsValLabels[3], palStrs[getPaletteSetting() % 3]);

    const char* scaleStrs[5] = {"Nearest", "Bilinear", "Bicubic", "Lanczos", "TAAU"};
    if (settingsValLabels[4]) lv_label_set_text(settingsValLabels[4], scaleStrs[getScalingModeSetting() % 5]);

    const char* unitStrs[3] = {"Celsius (" UI_SYMBOL_DEGREE "C)", "Fahrenheit (" UI_SYMBOL_DEGREE "F)", "Kelvin (K)"};
    if (settingsValLabels[5]) lv_label_set_text(settingsValLabels[5], unitStrs[getTempUnitSetting() % 3]);

    if (settingsValLabels[6]) {
        lv_label_set_text(settingsValLabels[6], (getI2CSpeedSetting() == I2C_SPEED_800K) ? "800 kHz" : "400 kHz");
    }

    const char* onOffStrs[2] = {"OFF", "ON"};
    if (settingsValLabels[7]) {
        lv_label_set_text(settingsValLabels[7], onOffStrs[getRgbLedEnabledSetting() ? 1 : 0]);
    }

    if (settingsValLabels[8]) {
        if (!getBLEEnabledSetting()) {
            lv_label_set_text(settingsValLabels[8], "OFF");
        } else if (isBLEConnected()) {
            lv_label_set_text(settingsValLabels[8], "LINKED");
        } else {
            lv_label_set_text(settingsValLabels[8], "ON");
        }
        lv_obj_set_style_text_color(settingsValLabels[8], UI_COLOR_ACCENT, 0);
    }
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

    // Top-right Battery Indicator (Percentage to the left of the icon)
    createBatteryIndicator(screen_settings, true, LV_ALIGN_TOP_RIGHT, -14, 11);

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
    int curY = 2;

    auto addSectionHeader = [&](const char* title) {
        lv_obj_t* secHdr = lv_label_create(settings_list);
        lv_label_set_text(secHdr, title);
        lv_obj_set_pos(secHdr, 4, curY);
        lv_obj_set_style_text_color(secHdr, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(secHdr, &lv_font_montserrat_10, 0);
        curY += 16;
    };

    auto addDivider = [&](lv_point_t* pts) {
        curY += 8;
        lv_obj_t* sep = lv_line_create(settings_list);
        lv_line_set_points(sep, pts, 2);
        lv_obj_add_style(sep, &style_divider, 0);
        lv_obj_set_pos(sep, 0, curY);
        curY += 12;
    };

    auto createSettingBtn = [&](int i) {
        settingsBtns[i] = lv_btn_create(settings_list);
        lv_obj_set_pos(settingsBtns[i], 2, curY);
        lv_obj_set_size(settingsBtns[i], 198, itemH);
        lv_obj_add_style(settingsBtns[i], &style_btn_default, 0);
        if (i == SETTINGS_COUNT - 1) {
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
        if (i == SETTINGS_COUNT - 1) {
            lv_label_set_text(settingsArrows[i], LV_SYMBOL_LEFT);
            lv_obj_set_style_text_color(settingsArrows[i], lv_color_black(), 0);
        } else {
            lv_label_set_text(settingsArrows[i], LV_SYMBOL_RIGHT);
            lv_obj_set_style_text_color(settingsArrows[i], UI_COLOR_ACCENT, 0);
        }
        lv_obj_set_style_text_font(settingsArrows[i], &lv_font_montserrat_12, 0);
        lv_obj_align(settingsArrows[i], LV_ALIGN_LEFT_MID, 0, 0);
        if (i != 0) lv_obj_add_flag(settingsArrows[i], LV_OBJ_FLAG_HIDDEN);

        // Setting Name
        settingsNameLabels[i] = lv_label_create(settingsBtns[i]);
        lv_label_set_text(settingsNameLabels[i], settingsLabels[i]);
        lv_obj_align(settingsNameLabels[i], LV_ALIGN_LEFT_MID, 13, 0);
        lv_obj_set_style_text_font(settingsNameLabels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(settingsNameLabels[i], (i == SETTINGS_COUNT - 1) ? UI_COLOR_TEXT_DIM : UI_COLOR_TEXT, 0);

        // Setting Value (Right column)
        if (i < SETTINGS_COUNT - 1) {
            settingsValLabels[i] = lv_label_create(settingsBtns[i]);
            lv_obj_align(settingsValLabels[i], LV_ALIGN_RIGHT_MID, -3, 0);
            lv_obj_set_style_text_color(settingsValLabels[i], UI_COLOR_ACCENT, 0);
            lv_obj_set_style_text_font(settingsValLabels[i], &lv_font_montserrat_12, 0);
        } else {
            settingsValLabels[i] = nullptr;
        }
        lv_obj_add_event_cb(settingsBtns[i], settingsBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)i);
        curY += itemH + spacing;
    };

    static lv_point_t sep1_pts[] = {{4, 0}, {196, 0}};
    static lv_point_t sep2_pts[] = {{4, 0}, {196, 0}};
    static lv_point_t sep3_pts[] = {{4, 0}, {196, 0}};

    // Section 1: DISPLAY & VIEW
    addSectionHeader("DISPLAY & VIEW");
    createSettingBtn(0); // Brightness
    createSettingBtn(1); // Orientation
    createSettingBtn(2); // Mirror Feed
    createSettingBtn(3); // Color Palette

    // Divider 1
    addDivider(sep1_pts);

    // Section 2: THERMAL PROCESSING
    addSectionHeader("THERMAL PROCESSING");
    createSettingBtn(4); // Upscaling
    createSettingBtn(5); // Temp Unit

    // Divider 2
    addDivider(sep2_pts);

    // Section 3: HARDWARE & SYSTEM
    addSectionHeader("HARDWARE & SYSTEM");
    createSettingBtn(6); // I2C Clock
    createSettingBtn(7); // RGB LED
    createSettingBtn(8); // Bluetooth

    // Divider 3
    addDivider(sep3_pts);

    // Section 4: Exit Option
    createSettingBtn(9); // Back to Main Menu

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
