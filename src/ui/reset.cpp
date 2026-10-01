#include "ui.hpp"
#include "storage.hpp"

// Reset Confirmation Screen Objects
lv_obj_t* screen_reset = nullptr;
static lv_obj_t* resetBtns[2];
static lv_obj_t* resetArrows[2];
static lv_obj_t* resetLabels[2];
static lv_obj_t* resetIcons[2];

static const char* resetBtnTexts[2] = {
    "Cancel (Keep Settings)",
    "Reset All Settings"
};

static void resetBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        for (int i = 0; i < 2; ++i) {
            if (i == idx) {
                if (resetArrows[i]) lv_obj_clear_flag(resetArrows[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                if (resetArrows[i]) lv_obj_add_flag(resetArrows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (idx == 1) {
            // Confirm Reset button: text and arrow switch to black on red background
            if (resetLabels[1]) lv_obj_set_style_text_color(resetLabels[1], lv_color_black(), 0);
            if (resetArrows[1]) lv_obj_set_style_text_color(resetArrows[1], lv_color_black(), 0);
            if (resetIcons[1]) lv_obj_set_style_text_color(resetIcons[1], lv_color_black(), 0);
        } else {
            if (resetLabels[1]) lv_obj_set_style_text_color(resetLabels[1], UI_COLOR_TEXT_DIM, 0);
            if (resetIcons[1]) lv_obj_set_style_text_color(resetIcons[1], UI_COLOR_TEXT_DIM, 0);
        }
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (idx == 1) {
            if (resetLabels[1]) lv_obj_set_style_text_color(resetLabels[1], UI_COLOR_TEXT_DIM, 0);
            if (resetIcons[1]) lv_obj_set_style_text_color(resetIcons[1], UI_COLOR_TEXT_DIM, 0);
        }
    } else if (code == LV_EVENT_CLICKED) {
        if (idx == 0) {
            // Cancel -> Return to Settings screen focused on Factory Reset (index 9)
            Serial.println("[UI] Factory Reset cancelled.");
            uiNavToSettings(9);
        } else if (idx == 1) {
            // Confirm Reset -> restore defaults and return to Settings
            Serial.println("[UI] Factory Reset confirmed by user!");
            resetSettingsToDefaults();
            updateSettingsValues();
            uiNavToSettings(9);
        }
    }
}

void setupResetGroup() {
    for (int i = 0; i < 2; ++i) {
        if (resetBtns[i] != nullptr) {
            lv_group_add_obj(ui_wheel_group, resetBtns[i]);
        }
    }

    // Default focus on Cancel (safe default!)
    if (resetBtns[0] != nullptr) {
        lv_group_focus_obj(resetBtns[0]);
    }
    if (resetArrows[0] != nullptr) lv_obj_clear_flag(resetArrows[0], LV_OBJ_FLAG_HIDDEN);
    if (resetArrows[1] != nullptr) lv_obj_add_flag(resetArrows[1], LV_OBJ_FLAG_HIDDEN);
    if (resetLabels[1] != nullptr) lv_obj_set_style_text_color(resetLabels[1], UI_COLOR_TEXT_DIM, 0);
    if (resetIcons[1] != nullptr) lv_obj_set_style_text_color(resetIcons[1], UI_COLOR_TEXT_DIM, 0);
}

void buildResetScreen() {
    screen_reset = lv_obj_create(NULL);
    lv_obj_add_style(screen_reset, &style_screen, 0);
    lv_obj_clear_flag(screen_reset, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_reset, LV_SCROLLBAR_MODE_OFF);

    // Header Title
    lv_obj_t* header = lv_label_create(screen_reset);
    lv_label_set_text(header, "FACTORY RESET");
    lv_obj_add_style(header, &style_header_title, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 14, 9);

    // Top Divider Line
    static lv_point_t h_line_pts[] = {{0, 32}, {DISPLAY_WIDTH - 1, 32}};
    lv_obj_t* line_top = lv_line_create(screen_reset);
    lv_line_set_points(line_top, h_line_pts, 2);
    lv_obj_add_style(line_top, &style_divider, 0);

    // Top-Right Battery Indicator
    createBatteryIndicator(screen_reset, true, LV_ALIGN_TOP_RIGHT, -14, 11);

    // Warning Banner / Question Header
    lv_obj_t* question = lv_label_create(screen_reset);
    lv_label_set_text(question, LV_SYMBOL_WARNING "  Are you sure?");
    lv_obj_set_style_text_font(question, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(question, UI_COLOR_ACCENT, 0);
    lv_obj_align(question, LV_ALIGN_TOP_MID, 0, 44);

    // Subtitle Description
    lv_obj_t* desc = lv_label_create(screen_reset);
    lv_label_set_text(desc, "All settings will be restored to defaults.\nSaved NVS flash preferences will be erased.");
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(desc, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(desc, LV_ALIGN_TOP_MID, 0, 66);

    // Buttons Container (matching main menu style)
    static const int btnY[2] = {96, 144};
    for (int i = 0; i < 2; ++i) {
        resetBtns[i] = lv_btn_create(screen_reset);
        lv_obj_set_size(resetBtns[i], 260, 36);
        lv_obj_set_pos(resetBtns[i], 30, btnY[i]);
        lv_obj_add_style(resetBtns[i], &style_btn_default, 0);

        if (i == 1) {
            // "Reset All Settings" button: styled like Power Off button (red fill on focus, black text)
            lv_obj_add_style(resetBtns[i], &style_btn_back_focused, LV_STATE_FOCUSED);
            lv_obj_add_style(resetBtns[i], &style_btn_back_focused, LV_STATE_FOCUS_KEY);
        } else {
            // Cancel button: standard focus styling
            lv_obj_add_style(resetBtns[i], &style_btn_focused, LV_STATE_FOCUSED);
            lv_obj_add_style(resetBtns[i], &style_btn_focused, LV_STATE_FOCUS_KEY);
        }
        lv_obj_add_style(resetBtns[i], &style_btn_pressed, LV_STATE_PRESSED);

        // Selection Arrow Indicator
        resetArrows[i] = lv_label_create(resetBtns[i]);
        if (i == 1) {
            lv_label_set_text(resetArrows[i], LV_SYMBOL_LEFT);
            lv_obj_set_style_text_color(resetArrows[i], lv_color_black(), 0);
        } else {
            lv_label_set_text(resetArrows[i], LV_SYMBOL_RIGHT);
            lv_obj_set_style_text_color(resetArrows[i], UI_COLOR_ACCENT, 0);
        }
        lv_obj_set_style_text_font(resetArrows[i], &lv_font_montserrat_14, 0);
        lv_obj_align(resetArrows[i], LV_ALIGN_LEFT_MID, 6, 0);
        if (i != 0) lv_obj_add_flag(resetArrows[i], LV_OBJ_FLAG_HIDDEN);

        // Label
        resetLabels[i] = lv_label_create(resetBtns[i]);
        lv_label_set_text(resetLabels[i], resetBtnTexts[i]);
        lv_obj_align(resetLabels[i], LV_ALIGN_LEFT_MID, 24, 0);
        lv_obj_set_style_text_font(resetLabels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(resetLabels[i], (i == 1) ? UI_COLOR_TEXT_DIM : UI_COLOR_TEXT, 0);

        // Right-side Icon
        resetIcons[i] = lv_label_create(resetBtns[i]);
        lv_label_set_text(resetIcons[i], (i == 0) ? LV_SYMBOL_LEFT : LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_font(resetIcons[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(resetIcons[i], UI_COLOR_TEXT_DIM, 0);
        lv_obj_align(resetIcons[i], LV_ALIGN_RIGHT_MID, -12, 0);

        lv_obj_add_event_cb(resetBtns[i], resetBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)i);
    }

    // Bottom Divider Line
    static lv_point_t f_line_pts[] = {{0, 216}, {DISPLAY_WIDTH - 1, 216}};
    lv_obj_t* line_bot = lv_line_create(screen_reset);
    lv_line_set_points(line_bot, f_line_pts, 2);
    lv_obj_add_style(line_bot, &style_divider, 0);

    // Footer Navigation Hints
    lv_obj_t* footer = lv_label_create(screen_reset);
    lv_label_set_text(footer, "[Rotate] Select   |   [Click] Confirm");
    lv_obj_add_style(footer, &style_footer_text, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 14, -8);
}
