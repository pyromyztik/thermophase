#include "ui.hpp"
#include "display.hpp"
#include "storage.hpp"

// Thermal Range Screen Objects
lv_obj_t* screen_thermal_range = nullptr;

static constexpr int RANGE_ITEMS_COUNT = 5;
static lv_obj_t* rangeBtns[RANGE_ITEMS_COUNT];
static lv_obj_t* rangeArrows[RANGE_ITEMS_COUNT];
static lv_obj_t* rangeNameLabels[RANGE_ITEMS_COUNT];
static lv_obj_t* rangeCheckmarks[2];
static lv_obj_t* rangeValLabels[RANGE_ITEMS_COUNT];
static lv_obj_t* range_desc_label = nullptr;
static lv_obj_t* range_footer_label = nullptr;
static lv_obj_t* range_list = nullptr;

static const char* rangeDescriptions[RANGE_ITEMS_COUNT] = {
    "Auto: Dynamically adapts color gradient bounds to current scene min and max temperatures.",
    "Manual: Locks false-color gradient bounds to user-specified Min and Max temperatures.",
    "Min Temp: Lower bound temperature. Click to adjust with wheel (-20" UI_SYMBOL_DEGREE "C to Max-2" UI_SYMBOL_DEGREE "C). Click to lock.",
    "Max Temp: Upper bound temperature. Click to adjust with wheel (Min+2" UI_SYMBOL_DEGREE "C to 300" UI_SYMBOL_DEGREE "C). Click to lock.",
    "Return to previous screen."
};

static void updateThermalRangeValues();

static void updateThermalRangeDesc(int idx) {
    if (range_desc_label == nullptr || idx >= RANGE_ITEMS_COUNT) return;
    ThermalRangeMode mode = getThermalRangeModeSetting();

    if (idx == 0) {
        if (mode == THERMAL_RANGE_AUTO) {
            lv_label_set_text(range_desc_label, "This is the active mode.\n\nDynamically scales color palette to scene min and max temperatures.");
        } else {
            lv_label_set_text(range_desc_label, "Click to activate Automatic Mode.\n\nDynamically adapts false-color mapping to scene temperature extremes.");
        }
    } else if (idx == 1) {
        if (mode == THERMAL_RANGE_MANUAL) {
            lv_label_set_text(range_desc_label, "This is the active mode.\n\nLocks palette range between user-defined Min and Max temperatures.");
        } else {
            lv_label_set_text(range_desc_label, "Click to activate Manual Mode.\n\nLocks false-color gradient to fixed Min and Max temperatures.");
        }
    } else if (idx == 2 || idx == 3) {
        char buf[256];
        TempUnit unit = getTempUnitSetting();
        float minC = getManualMinTempSetting();
        float maxC = getManualMaxTempSetting();
        snprintf(buf, sizeof(buf), "%s\n\nCurrent Range:\n%.0f to %.0f %s",
                 rangeDescriptions[idx],
                 convertTemp(minC, unit),
                 convertTemp(maxC, unit),
                 getUnitSym(unit));
        lv_label_set_text(range_desc_label, buf);
    } else if (idx == 4) {
        if (getThermalRangeCaller() == APP_STATE_SETTINGS) {
            lv_label_set_text(range_desc_label, "Save thermal range settings and return to settings menu.");
        } else {
            lv_label_set_text(range_desc_label, "Save thermal range settings and return to live thermal camera view.");
        }
    }
}

static void updateThermalRangeValues() {
    ThermalRangeMode mode = getThermalRangeModeSetting();
    TempUnit unit = getTempUnitSetting();
    float minC = getManualMinTempSetting();
    float maxC = getManualMaxTempSetting();

    // Both Mode options (0 and 1) are ALWAYS bright and active
    if (rangeNameLabels[0] != nullptr) {
        lv_obj_set_style_text_color(rangeNameLabels[0], UI_COLOR_TEXT, 0);
    }
    if (rangeBtns[0] != nullptr) {
        lv_obj_set_style_border_color(rangeBtns[0], UI_COLOR_BORDER, 0);
    }

    if (rangeNameLabels[1] != nullptr) {
        lv_obj_set_style_text_color(rangeNameLabels[1], UI_COLOR_TEXT, 0);
    }
    if (rangeBtns[1] != nullptr) {
        lv_obj_set_style_border_color(rangeBtns[1], UI_COLOR_BORDER, 0);
    }

    // Green checkmark strictly reflects the currently active mode
    if (rangeCheckmarks[0] != nullptr) {
        if (mode == THERMAL_RANGE_AUTO) {
            lv_label_set_text(rangeCheckmarks[0], LV_SYMBOL_OK);
            lv_obj_clear_flag(rangeCheckmarks[0], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(rangeCheckmarks[0], "");
            lv_obj_add_flag(rangeCheckmarks[0], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (rangeCheckmarks[1] != nullptr) {
        if (mode == THERMAL_RANGE_MANUAL) {
            lv_label_set_text(rangeCheckmarks[1], LV_SYMBOL_OK);
            lv_obj_clear_flag(rangeCheckmarks[1], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(rangeCheckmarks[1], "");
            lv_obj_add_flag(rangeCheckmarks[1], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Min Temp (Option 2) and Max Temp (Option 3):
    // "only the min/max temps should be [darkened] when auto is chosen."
    bool manualActive = (mode == THERMAL_RANGE_MANUAL);
    lv_color_t tempTextColor = manualActive ? UI_COLOR_TEXT : lv_color_hex(0x555555);
    lv_color_t tempBorderColor = manualActive ? UI_COLOR_BORDER : lv_color_hex(0x333333);

    // Min Temp
    if (rangeNameLabels[2] != nullptr) {
        lv_obj_set_style_text_color(rangeNameLabels[2], tempTextColor, 0);
    }
    if (rangeValLabels[2] != nullptr) {
        char buf[24];
        formatTempStr(buf, sizeof(buf), minC, unit, 0, true);
        lv_label_set_text(rangeValLabels[2], buf);
        lv_obj_set_style_text_color(rangeValLabels[2], tempTextColor, 0);
    }
    if (rangeBtns[2] != nullptr) {
        lv_obj_set_style_border_color(rangeBtns[2], tempBorderColor, 0);
    }

    // Max Temp
    if (rangeNameLabels[3] != nullptr) {
        lv_obj_set_style_text_color(rangeNameLabels[3], tempTextColor, 0);
    }
    if (rangeValLabels[3] != nullptr) {
        char buf[24];
        formatTempStr(buf, sizeof(buf), maxC, unit, 0, true);
        lv_label_set_text(rangeValLabels[3], buf);
        lv_obj_set_style_text_color(rangeValLabels[3], tempTextColor, 0);
    }
    if (rangeBtns[3] != nullptr) {
        lv_obj_set_style_border_color(rangeBtns[3], tempBorderColor, 0);
    }
}

static void updateItemEditVisual(int idx, bool isEditing) {
    if (idx < 0 || idx >= RANGE_ITEMS_COUNT || rangeBtns[idx] == nullptr) return;

    if (isEditing) {
        lv_obj_set_style_border_color(rangeBtns[idx], UI_COLOR_YELLOW, 0);
        lv_obj_set_style_border_width(rangeBtns[idx], 2, 0);
        if (rangeArrows[idx]) lv_obj_set_style_text_color(rangeArrows[idx], UI_COLOR_YELLOW, 0);
        if (rangeValLabels[idx]) lv_obj_set_style_text_color(rangeValLabels[idx], UI_COLOR_YELLOW, 0);
        if (range_footer_label) {
            lv_label_set_text(range_footer_label, "[Rotate] Adjust Temp   |   [Click] Lock & Set");
        }
        if (range_desc_label) {
            lv_label_set_text(range_desc_label, "ADJUSTING TEMPERATURE\n\nRotate wheel to adjust value.\nClick wheel to lock and set.");
        }
    } else {
        lv_obj_set_style_border_width(rangeBtns[idx], 1, 0);
        if (rangeArrows[idx]) lv_obj_set_style_text_color(rangeArrows[idx], UI_COLOR_ACCENT, 0);
        if (range_footer_label) {
            lv_label_set_text(range_footer_label, "[Rotate] Navigate   |   [Click] Select / Back");
        }
        updateThermalRangeValues();
        updateThermalRangeDesc(idx);
    }
}

static void rangeBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        for (int i = 0; i < RANGE_ITEMS_COUNT; ++i) {
            if (i == idx) {
                if (rangeArrows[i]) lv_obj_clear_flag(rangeArrows[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                if (rangeArrows[i]) lv_obj_add_flag(rangeArrows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (idx == 4) {
            // Back button
            if (rangeNameLabels[4]) lv_obj_set_style_text_color(rangeNameLabels[4], lv_color_black(), 0);
            if (rangeArrows[4]) lv_obj_set_style_text_color(rangeArrows[4], lv_color_black(), 0);
        } else if (rangeNameLabels[4] != nullptr) {
            lv_obj_set_style_text_color(rangeNameLabels[4], UI_COLOR_TEXT_DIM, 0);
        }

        updateThermalRangeDesc(idx);

    } else if (code == LV_EVENT_DEFOCUSED) {
        if (idx == 4 && rangeNameLabels[4] != nullptr) {
            lv_obj_set_style_text_color(rangeNameLabels[4], UI_COLOR_TEXT_DIM, 0);
        }
        if (idx == 2 || idx == 3) {
            updateItemEditVisual(idx, false);
        }

    } else if (code == LV_EVENT_CLICKED) {
        switch (idx) {
            case 0: // Automatic
                setThermalRangeModeSetting(THERMAL_RANGE_AUTO);
                updateThermalRangeValues();
                updateThermalRangeDesc(idx);
                break;

            case 1: // Manual
                setThermalRangeModeSetting(THERMAL_RANGE_MANUAL);
                updateThermalRangeValues();
                updateThermalRangeDesc(idx);
                break;

            case 2: { // Min Temp
                if (getThermalRangeModeSetting() != THERMAL_RANGE_MANUAL) {
                    setThermalRangeModeSetting(THERMAL_RANGE_MANUAL);
                    updateThermalRangeValues();
                }
                bool isEditing = !lv_group_get_editing(ui_wheel_group);
                lv_group_set_editing(ui_wheel_group, isEditing);
                updateItemEditVisual(2, isEditing);
                break;
            }

            case 3: { // Max Temp
                if (getThermalRangeModeSetting() != THERMAL_RANGE_MANUAL) {
                    setThermalRangeModeSetting(THERMAL_RANGE_MANUAL);
                    updateThermalRangeValues();
                }
                bool isEditing = !lv_group_get_editing(ui_wheel_group);
                lv_group_set_editing(ui_wheel_group, isEditing);
                updateItemEditVisual(3, isEditing);
                break;
            }

            case 4: // Back
                saveSettingsIfDirty();
                updateSettingsValues();
                if (getThermalRangeCaller() == APP_STATE_SETTINGS) {
                    uiNavToSettings(SETTING_THERMAL_RANGE);
                } else {
                    uiNavToThermal();
                }
                break;
        }

    } else if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        float minC = getManualMinTempSetting();
        float maxC = getManualMaxTempSetting();

        if (idx == 2) {
            // Adjusting Min Temp
            if (key == LV_KEY_RIGHT || key == LV_KEY_UP) {
                if (minC + 1.0f <= maxC - 2.0f) {
                    setManualMinTempSetting(minC + 1.0f);
                    updateThermalRangeValues();
                    updateThermalRangeDesc(idx);
                }
            } else if (key == LV_KEY_LEFT || key == LV_KEY_DOWN) {
                if (minC - 1.0f >= -20.0f) {
                    setManualMinTempSetting(minC - 1.0f);
                    updateThermalRangeValues();
                    updateThermalRangeDesc(idx);
                }
            }
        } else if (idx == 3) {
            // Adjusting Max Temp
            if (key == LV_KEY_RIGHT || key == LV_KEY_UP) {
                if (maxC + 1.0f <= 300.0f) {
                    setManualMaxTempSetting(maxC + 1.0f);
                    updateThermalRangeValues();
                    updateThermalRangeDesc(idx);
                }
            } else if (key == LV_KEY_LEFT || key == LV_KEY_DOWN) {
                if (maxC - 1.0f >= minC + 2.0f) {
                    setManualMaxTempSetting(maxC - 1.0f);
                    updateThermalRangeValues();
                    updateThermalRangeDesc(idx);
                }
            }
        }
    }
}

void setupThermalRangeGroup() {
    for (int i = 0; i < RANGE_ITEMS_COUNT; ++i) {
        if (rangeBtns[i] != nullptr) {
            lv_group_add_obj(ui_wheel_group, rangeBtns[i]);
        }
    }

    // Adapt Back button text depending on whether entered from Settings or Live View
    if (rangeNameLabels[4] != nullptr) {
        if (getThermalRangeCaller() == APP_STATE_SETTINGS) {
            lv_label_set_text(rangeNameLabels[4], "Back to Settings");
        } else {
            lv_label_set_text(rangeNameLabels[4], "Back to Live View");
        }
    }

    // Default focus MUST be opposite to what is currently selected:
    // If currently AUTO (0), focus on MANUAL (1).
    // If currently MANUAL (1), focus on AUTO (0).
    int initialFocus = (getThermalRangeModeSetting() == THERMAL_RANGE_AUTO) ? 1 : 0;

    for (int i = 0; i < RANGE_ITEMS_COUNT; ++i) {
        if (i == initialFocus) {
            if (rangeArrows[i]) lv_obj_clear_flag(rangeArrows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            if (rangeArrows[i]) lv_obj_add_flag(rangeArrows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (rangeBtns[initialFocus] != nullptr) {
        lv_group_focus_obj(rangeBtns[initialFocus]);
    }

    updateThermalRangeValues();
    updateThermalRangeDesc(initialFocus);
}

void buildThermalRangeScreen() {
    screen_thermal_range = lv_obj_create(NULL);
    lv_obj_add_style(screen_thermal_range, &style_screen, 0);
    lv_obj_clear_flag(screen_thermal_range, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_thermal_range, LV_SCROLLBAR_MODE_OFF);

    // 1. Header Title
    lv_obj_t* header = lv_label_create(screen_thermal_range);
    lv_label_set_text(header, "THERMAL RANGE");
    lv_obj_add_style(header, &style_header_title, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 14, 9);

    // 2. Header Divider Line
    static lv_point_t h_line_pts[] = {{0, 32}, {DISPLAY_WIDTH - 1, 32}};
    lv_obj_t* line_top = lv_line_create(screen_thermal_range);
    lv_line_set_points(line_top, h_line_pts, 2);
    lv_obj_add_style(line_top, &style_divider, 0);

    // Top-right Battery Indicator
    createBatteryIndicator(screen_thermal_range, true, LV_ALIGN_TOP_RIGHT, -14, 11);

    // Vertical Divider Line separating options from right-side description panel
    static lv_point_t v_line_pts[] = {{216, 32}, {216, 216}};
    lv_obj_t* line_vert = lv_line_create(screen_thermal_range);
    lv_line_set_points(line_vert, v_line_pts, 2);
    lv_obj_add_style(line_vert, &style_divider, 0);

    // 3. Left Options Container
    range_list = lv_obj_create(screen_thermal_range);
    lv_obj_set_pos(range_list, 4, 34);
    lv_obj_set_size(range_list, 208, 180);
    lv_obj_set_style_bg_opa(range_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(range_list, 0, 0);
    lv_obj_set_style_pad_all(range_list, 0, 0);
    lv_obj_clear_flag(range_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(range_list, LV_SCROLLBAR_MODE_OFF);

    const char* itemNames[RANGE_ITEMS_COUNT] = {
        "Automatic Mode",
        "Manual Mode",
        "Min Temperature",
        "Max Temperature",
        "Back to Live View"
    };

    lv_coord_t curY = 2;
    constexpr lv_coord_t itemH = 26;
    constexpr lv_coord_t spacing = 4;

    for (int i = 0; i < RANGE_ITEMS_COUNT; ++i) {
        if (i == 2 || i == 4) {
            // Divider before Min/Max temp group and before Back button
            static lv_point_t div_pts[] = {{4, 0}, {196, 0}};
            lv_obj_t* div = lv_line_create(range_list);
            lv_line_set_points(div, div_pts, 2);
            lv_obj_add_style(div, &style_divider, 0);
            lv_obj_set_pos(div, 0, curY + 2);
            curY += 7;
        }

        rangeBtns[i] = lv_btn_create(range_list);
        lv_obj_set_pos(rangeBtns[i], 2, curY);
        lv_obj_set_size(rangeBtns[i], 198, itemH);
        lv_obj_add_style(rangeBtns[i], &style_btn_default, 0);

        if (i == 4) {
            lv_obj_add_style(rangeBtns[i], &style_btn_back_focused, LV_STATE_FOCUSED);
            lv_obj_add_style(rangeBtns[i], &style_btn_back_focused, LV_STATE_FOCUS_KEY);
        } else {
            lv_obj_add_style(rangeBtns[i], &style_btn_focused, LV_STATE_FOCUSED);
            lv_obj_add_style(rangeBtns[i], &style_btn_focused, LV_STATE_FOCUS_KEY);
        }
        lv_obj_add_style(rangeBtns[i], &style_btn_pressed, LV_STATE_PRESSED);

        // Arrow
        rangeArrows[i] = lv_label_create(rangeBtns[i]);
        lv_label_set_text(rangeArrows[i], (i == 4) ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(rangeArrows[i], (i == 4) ? lv_color_black() : UI_COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(rangeArrows[i], &lv_font_montserrat_12, 0);
        lv_obj_align(rangeArrows[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_flag(rangeArrows[i], LV_OBJ_FLAG_HIDDEN);

        // Name
        rangeNameLabels[i] = lv_label_create(rangeBtns[i]);
        lv_label_set_text(rangeNameLabels[i], itemNames[i]);
        lv_obj_align(rangeNameLabels[i], LV_ALIGN_LEFT_MID, 13, 0);
        lv_obj_set_style_text_font(rangeNameLabels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(rangeNameLabels[i], (i == 4) ? UI_COLOR_TEXT_DIM : UI_COLOR_TEXT, 0);

        // Options 0 and 1: Green checkmarks
        if (i < 2) {
            rangeCheckmarks[i] = lv_label_create(rangeBtns[i]);
            lv_obj_align(rangeCheckmarks[i], LV_ALIGN_RIGHT_MID, -8, 0);
            lv_obj_set_style_text_color(rangeCheckmarks[i], lv_color_hex(0x008C00), 0);
            lv_obj_set_style_text_font(rangeCheckmarks[i], &lv_font_montserrat_12, 0);
            lv_label_set_text(rangeCheckmarks[i], "");
            lv_obj_add_flag(rangeCheckmarks[i], LV_OBJ_FLAG_HIDDEN);
            rangeValLabels[i] = nullptr;
        } else if (i == 2 || i == 3) {
            // Options 2 and 3: Temperature value readouts
            rangeValLabels[i] = lv_label_create(rangeBtns[i]);
            lv_obj_align(rangeValLabels[i], LV_ALIGN_RIGHT_MID, -6, 0);
            lv_obj_set_style_text_font(rangeValLabels[i], &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(rangeValLabels[i], UI_COLOR_TEXT, 0);
            lv_label_set_text(rangeValLabels[i], "");
        } else {
            rangeValLabels[i] = nullptr;
        }

        lv_obj_add_event_cb(rangeBtns[i], rangeBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)i);
        curY += itemH + spacing;
    }

    // 4. Right-Side Contextual Description Panel
    lv_obj_t* hint_hdr = lv_label_create(screen_thermal_range);
    lv_label_set_text(hint_hdr, "INFO");
    lv_obj_set_pos(hint_hdr, 222, 38);
    lv_obj_set_style_text_color(hint_hdr, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint_hdr, &lv_font_montserrat_10, 0);

    range_desc_label = lv_label_create(screen_thermal_range);
    lv_obj_set_pos(range_desc_label, 222, 54);
    lv_obj_set_size(range_desc_label, 92, 150);
    lv_label_set_long_mode(range_desc_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(range_desc_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(range_desc_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(range_desc_label, rangeDescriptions[0]);

    // 5. Footer Divider Line
    static lv_point_t f_line_pts[] = {{0, 216}, {DISPLAY_WIDTH - 1, 216}};
    lv_obj_t* line_bot = lv_line_create(screen_thermal_range);
    lv_line_set_points(line_bot, f_line_pts, 2);
    lv_obj_add_style(line_bot, &style_divider, 0);

    // 6. Footer Navigation Hints
    range_footer_label = lv_label_create(screen_thermal_range);
    lv_label_set_text(range_footer_label, "[Rotate] Navigate   |   [Click] Select / Back");
    lv_obj_add_style(range_footer_label, &style_footer_text, 0);
    lv_obj_align(range_footer_label, LV_ALIGN_BOTTOM_LEFT, 14, -8);
}
