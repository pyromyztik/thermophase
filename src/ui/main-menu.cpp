#include "ui.hpp"
#include "power.hpp"
#include "ble.hpp"

// Main Menu Shared Variables
lv_obj_t* main_menu_list = nullptr;
lv_obj_t* mainMenuBtns[MAIN_MENU_COUNT];
lv_obj_t* mainMenuArrows[MAIN_MENU_COUNT];
static lv_obj_t* mainMenuLabels_objs[MAIN_MENU_COUNT];
static lv_obj_t* mainMenuIcons[MAIN_MENU_COUNT];

static const char* mainMenuLabels[MAIN_MENU_COUNT] = {
    "Live Thermal View",
    "Pulsed Phase (PPT)",
    "Stream over BLE",
    "Settings",
    "Power Off"
};

static const char* mainMenuGlyphs[MAIN_MENU_COUNT] = {
    LV_SYMBOL_PLAY,
    LV_SYMBOL_STOP,
    UI_SYMBOL_CIRCLE,
    LV_SYMBOL_SETTINGS,
    LV_SYMBOL_POWER
};

static void mainMenuBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
            if (i == idx) {
                lv_obj_clear_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        // Power Off item (index 4) is styled like a back/exit button:
        // fills with red, text and icons switch to black
        if (idx == 4) {
            if (mainMenuLabels_objs[4]) lv_obj_set_style_text_color(mainMenuLabels_objs[4], lv_color_black(), 0);
            if (mainMenuArrows[4]) lv_obj_set_style_text_color(mainMenuArrows[4], lv_color_black(), 0);
            if (mainMenuIcons[4]) lv_obj_set_style_text_color(mainMenuIcons[4], lv_color_black(), 0);
        } else {
            if (mainMenuLabels_objs[4]) lv_obj_set_style_text_color(mainMenuLabels_objs[4], UI_COLOR_TEXT_DIM, 0);
            if (mainMenuIcons[4]) lv_obj_set_style_text_color(mainMenuIcons[4], UI_COLOR_TEXT_DIM, 0);
        }

        // Ensure focused item is smoothly brought into view in scrollable container
        if (main_menu_list != nullptr) {
            if (idx == 0) {
                lv_obj_scroll_to_y(main_menu_list, 0, LV_ANIM_ON);
            } else {
                lv_obj_scroll_to_view(mainMenuBtns[idx], LV_ANIM_ON);
            }
        }
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (idx == 4) {
            if (mainMenuLabels_objs[4]) lv_obj_set_style_text_color(mainMenuLabels_objs[4], UI_COLOR_TEXT_DIM, 0);
            if (mainMenuIcons[4]) lv_obj_set_style_text_color(mainMenuIcons[4], UI_COLOR_TEXT_DIM, 0);
        }
    } else if (code == LV_EVENT_CLICKED) {
        if (idx == 0) {
            uiNavToThermal();
        } else if (idx == 1) {
            uiNavToPPT();
        } else if (idx == 2) {
            bool nextState = !isBLEStreaming();
            setBLEStreaming(nextState);
            refreshMainMenuBLEState(false);
        } else if (idx == 3) {
            uiNavToSettings();
        } else if (idx == 4) {
            // Power off action with smooth fade out
            Serial.println("[UI] Powering Off System...");
            enterDeepSleep();
        }
    }
}

static int s_currentLayoutCount = -1;

void refreshMainMenuBLEState(bool resetFocus) {
    if (!screen_main_menu) return;

    bool bleConnected = isBLEEnabled() && isBLEConnected();
    int neededCount = bleConnected ? 5 : 4;

    // 1. Update button 2 state labels & glyphs (always safe)
    if (mainMenuLabels_objs[2] && mainMenuIcons[2]) {
        if (isBLEStreaming()) {
            lv_label_set_text(mainMenuLabels_objs[2], "Stop BLE Stream");
            lv_obj_set_style_text_color(mainMenuLabels_objs[2], UI_COLOR_TEXT, 0);
            lv_label_set_text(mainMenuIcons[2], UI_SYMBOL_CIRCLE);
            lv_obj_set_style_text_font(mainMenuIcons[2], &ui_font_circle, 0);
            lv_obj_set_style_text_color(mainMenuIcons[2], UI_COLOR_RED, 0);
        } else {
            lv_label_set_text(mainMenuLabels_objs[2], "Stream over BLE");
            lv_obj_set_style_text_color(mainMenuLabels_objs[2], UI_COLOR_TEXT, 0);
            lv_label_set_text(mainMenuIcons[2], UI_SYMBOL_CIRCLE);
            lv_obj_set_style_text_font(mainMenuIcons[2], &ui_font_circle, 0);
            lv_obj_set_style_text_color(mainMenuIcons[2], UI_COLOR_TEXT_DIM, 0);
        }
    }

    // 2. Only rebuild the group and reposition widgets if layout structure changed or resetFocus requested
    if (neededCount == s_currentLayoutCount && !resetFocus) {
        return;
    }
    s_currentLayoutCount = neededCount;

    lv_obj_t* prevFocused = ui_wheel_group ? lv_group_get_focused(ui_wheel_group) : nullptr;
    if (ui_wheel_group) {
        lv_group_remove_all_objs(ui_wheel_group);
    }

    if (bleConnected) {
        // 5 items layout: height 36 px (fixed size), Y positions {8, 50, 92, 134, 176} inside scrollable container
        static const int itemY5[5] = {8, 50, 92, 134, 176};
        for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
            lv_obj_set_size(mainMenuBtns[i], 260, 36);
            lv_obj_set_pos(mainMenuBtns[i], 30, itemY5[i]);
            lv_obj_clear_flag(mainMenuBtns[i], LV_OBJ_FLAG_HIDDEN);
            if (ui_wheel_group) {
                lv_group_add_obj(ui_wheel_group, mainMenuBtns[i]);
            }
        }

        if (resetFocus || prevFocused == nullptr) {
            if (main_menu_list) lv_obj_scroll_to_y(main_menu_list, 0, LV_ANIM_OFF);
            if (ui_wheel_group) lv_group_focus_obj(mainMenuBtns[0]);
            for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
                if (i == 0) lv_obj_clear_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
                else lv_obj_add_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            if (ui_wheel_group) lv_group_focus_obj(prevFocused);
        }
    } else {
        // 4 items layout: height 36 px (fixed size), Y positions {9, 51, 93, 135} (fits without scrolling)
        static const int itemY4[4] = {9, 51, 93, 135};
        int mapIdx[4] = {0, 1, 3, 4};

        // Hide stream button
        lv_obj_add_flag(mainMenuBtns[2], LV_OBJ_FLAG_HIDDEN);

        for (int slot = 0; slot < 4; ++slot) {
            int i = mapIdx[slot];
            lv_obj_set_size(mainMenuBtns[i], 260, 36);
            lv_obj_set_pos(mainMenuBtns[i], 30, itemY4[slot]);
            lv_obj_clear_flag(mainMenuBtns[i], LV_OBJ_FLAG_HIDDEN);
            if (ui_wheel_group) {
                lv_group_add_obj(ui_wheel_group, mainMenuBtns[i]);
            }
        }

        if (main_menu_list) lv_obj_scroll_to_y(main_menu_list, 0, LV_ANIM_OFF);

        if (resetFocus || prevFocused == nullptr || prevFocused == mainMenuBtns[2]) {
            if (ui_wheel_group) lv_group_focus_obj(mainMenuBtns[0]);
            for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
                if (i == 0) lv_obj_clear_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
                else lv_obj_add_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            if (ui_wheel_group) lv_group_focus_obj(prevFocused);
        }
    }
}

void buildMainMenuScreen() {
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

    // Top-right Battery Indicator (Percentage to the left of the icon)
    createBatteryIndicator(screen_main_menu, true, LV_ALIGN_TOP_RIGHT, -14, 11);

    // Scrollable Menu Buttons List Container (Y=34, H=180, fits between top divider and bottom divider)
    main_menu_list = lv_obj_create(screen_main_menu);
    lv_obj_set_pos(main_menu_list, 0, 34);
    lv_obj_set_size(main_menu_list, DISPLAY_WIDTH, 180);
    lv_obj_set_style_bg_opa(main_menu_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_menu_list, 0, 0);
    lv_obj_set_style_pad_all(main_menu_list, 0, 0);
    lv_obj_set_style_pad_bottom(main_menu_list, 8, 0);
    lv_obj_set_scroll_dir(main_menu_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(main_menu_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(main_menu_list, UI_COLOR_FOCUS_GREY, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(main_menu_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(main_menu_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(main_menu_list, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(main_menu_list, 4, LV_PART_SCROLLBAR);

    // Menu Buttons (All 5 items created as children of main_menu_list; sized & positioned dynamically)
    for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
        mainMenuBtns[i] = lv_btn_create(main_menu_list);
        lv_obj_set_size(mainMenuBtns[i], 260, 36);
        lv_obj_add_style(mainMenuBtns[i], &style_btn_default, 0);
        if (i == 4) {
            // Power Off button: styled like back/exit button (filled with red on focus)
            lv_obj_add_style(mainMenuBtns[i], &style_btn_back_focused, LV_STATE_FOCUSED);
            lv_obj_add_style(mainMenuBtns[i], &style_btn_back_focused, LV_STATE_FOCUS_KEY);
        } else {
            lv_obj_add_style(mainMenuBtns[i], &style_btn_focused, LV_STATE_FOCUSED);
            lv_obj_add_style(mainMenuBtns[i], &style_btn_focused, LV_STATE_FOCUS_KEY);
        }
        lv_obj_add_style(mainMenuBtns[i], &style_btn_pressed, LV_STATE_PRESSED);

        // Selection Arrow Indicator
        mainMenuArrows[i] = lv_label_create(mainMenuBtns[i]);
        if (i == 4) {
            lv_label_set_text(mainMenuArrows[i], LV_SYMBOL_LEFT);
            lv_obj_set_style_text_color(mainMenuArrows[i], lv_color_black(), 0);
        } else {
            lv_label_set_text(mainMenuArrows[i], LV_SYMBOL_RIGHT);
            lv_obj_set_style_text_color(mainMenuArrows[i], UI_COLOR_ACCENT, 0);
        }
        lv_obj_set_style_text_font(mainMenuArrows[i], &lv_font_montserrat_14, 0);
        lv_obj_align(mainMenuArrows[i], LV_ALIGN_LEFT_MID, 6, 0);
        if (i != 0) lv_obj_add_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);

        // Menu Label
        mainMenuLabels_objs[i] = lv_label_create(mainMenuBtns[i]);
        lv_label_set_text(mainMenuLabels_objs[i], mainMenuLabels[i]);
        lv_obj_align(mainMenuLabels_objs[i], LV_ALIGN_LEFT_MID, 24, 0);
        lv_obj_set_style_text_color(mainMenuLabels_objs[i], (i == 4) ? UI_COLOR_TEXT_DIM : UI_COLOR_TEXT, 0);

        // Right-side icon glyph
        mainMenuIcons[i] = lv_label_create(mainMenuBtns[i]);
        lv_label_set_text(mainMenuIcons[i], mainMenuGlyphs[i]);
        if (i == 2) {
            lv_obj_set_style_text_font(mainMenuIcons[i], &ui_font_circle, 0);
            lv_obj_set_style_text_color(mainMenuIcons[i], UI_COLOR_TEXT_DIM, 0);
        } else {
            lv_obj_set_style_text_font(mainMenuIcons[i], &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(mainMenuIcons[i], UI_COLOR_TEXT_DIM, 0);
        }
        lv_obj_align(mainMenuIcons[i], LV_ALIGN_RIGHT_MID, -12, 0);

        lv_obj_add_event_cb(mainMenuBtns[i], mainMenuBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)i);
    }

    // Apply dynamic initial layout based on BLE connectivity
    refreshMainMenuBLEState(true);

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
