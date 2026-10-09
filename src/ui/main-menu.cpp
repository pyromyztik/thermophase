#include "ui.hpp"
#include "power.hpp"
#include "ble.hpp"
#include "wifi.hpp"

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
    "Stream over Wi-Fi",
    "Settings",
    "Power Off"
};

static const char* mainMenuGlyphs[MAIN_MENU_COUNT] = {
    LV_SYMBOL_PLAY,
    LV_SYMBOL_STOP,
    UI_SYMBOL_CIRCLE,
    LV_SYMBOL_WIFI,
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
        // Power Off item (index 5) is styled like a back/exit button:
        // fills with red, text and icons switch to black
        if (idx == 5) {
            if (mainMenuLabels_objs[5]) lv_obj_set_style_text_color(mainMenuLabels_objs[5], lv_color_black(), 0);
            if (mainMenuArrows[5]) lv_obj_set_style_text_color(mainMenuArrows[5], lv_color_black(), 0);
            if (mainMenuIcons[5]) lv_obj_set_style_text_color(mainMenuIcons[5], lv_color_black(), 0);
        } else {
            if (mainMenuLabels_objs[5]) lv_obj_set_style_text_color(mainMenuLabels_objs[5], UI_COLOR_TEXT_DIM, 0);
            if (mainMenuIcons[5]) lv_obj_set_style_text_color(mainMenuIcons[5], UI_COLOR_TEXT_DIM, 0);
        }

        // Ensure focused item is smoothly brought into view in scrollable container
        if (main_menu_list != nullptr) {
            lv_anim_enable_t animMode = (lv_scr_act() == screen_main_menu) ? LV_ANIM_ON : LV_ANIM_OFF;
            if (idx == 0) {
                lv_obj_scroll_to_y(main_menu_list, 0, animMode);
            } else {
                lv_obj_scroll_to_view(mainMenuBtns[idx], animMode);
            }
        }
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (idx == 5) {
            if (mainMenuLabels_objs[5]) lv_obj_set_style_text_color(mainMenuLabels_objs[5], UI_COLOR_TEXT_DIM, 0);
            if (mainMenuIcons[5]) lv_obj_set_style_text_color(mainMenuIcons[5], UI_COLOR_TEXT_DIM, 0);
        }
    } else if (code == LV_EVENT_CLICKED) {
        if (idx == 0) {
            uiNavToThermal();
        } else if (idx == 1) {
            uiNavToPPT();
        } else if (idx == 2) {
            bool nextState = !isBLEStreaming();
            setBLEStreaming(nextState);
            refreshMainMenuWirelessState(false);
        } else if (idx == 3) {
            // Stream over Wi-Fi clicked: enter dedicated low-memory streaming mode
            uiNavToWiFiStream();
        } else if (idx == 4) {
            uiNavToSettings();
        } else if (idx == 5) {
            // Power off action with smooth fade out
            Serial.println("[UI] Powering Off System...");
            enterDeepSleep();
        }
    }
}

static int s_currentLayoutCount = -1;
static bool s_lastBleConnected = false;
static bool s_lastWifiEnabled = false;
static bool s_lastWifiConnected = false;

void refreshMainMenuWirelessState(bool resetFocus) {
    if (!screen_main_menu) return;

    bool bleConnected = isBLEEnabled() && isBLEConnected();
    bool wifiEnabled = isWiFiStreamingEnabled();
    bool wifiConnected = wifiEnabled && isWiFiClientConnected();

    // 1. Update BLE button state labels & glyphs
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

    // 2. Update Wi-Fi button state labels & glyphs
    if (mainMenuLabels_objs[3] && mainMenuIcons[3]) {
        lv_label_set_text(mainMenuLabels_objs[3], "Stream over Wi-Fi");
        lv_obj_set_style_text_color(mainMenuLabels_objs[3], UI_COLOR_TEXT, 0);
        lv_label_set_text(mainMenuIcons[3], LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(mainMenuIcons[3], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(mainMenuIcons[3], wifiConnected ? lv_color_hex(0x2196F3) : UI_COLOR_CYAN, 0);
    }

    // Determine visible items list
    int mapIdx[MAIN_MENU_COUNT];
    int neededCount = 0;
    mapIdx[neededCount++] = 0; // Live Thermal View
    mapIdx[neededCount++] = 1; // PPT
    if (bleConnected) {
        mapIdx[neededCount++] = 2; // Stream over BLE
    }
    if (wifiEnabled) {
        mapIdx[neededCount++] = 3; // Stream over Wi-Fi
    }
    mapIdx[neededCount++] = 4; // Settings
    mapIdx[neededCount++] = 5; // Power Off

    // 3. Only rebuild layout and wheel group if structure changed or resetFocus requested
    bool layoutChanged = (neededCount != s_currentLayoutCount) ||
                         (bleConnected != s_lastBleConnected) ||
                         (wifiEnabled != s_lastWifiEnabled) ||
                         (wifiConnected != s_lastWifiConnected);

    if (!layoutChanged && !resetFocus) {
        return;
    }

    bool isMainMenuActive = (getAppState() == APP_STATE_MAIN_MENU);

    // If main menu is not the active state, DO NOT touch ui_wheel_group!
    // Instead mark the layout dirty so when uiNavToMainMenu() runs, it rebuilds completely.
    if (!isMainMenuActive && getAppState() != APP_STATE_BOOT) {
        s_currentLayoutCount = -1;
        s_lastBleConnected = bleConnected;
        s_lastWifiEnabled = wifiEnabled;
        s_lastWifiConnected = wifiConnected;
        return;
    }

    s_currentLayoutCount = neededCount;
    s_lastBleConnected = bleConnected;
    s_lastWifiEnabled = wifiEnabled;
    s_lastWifiConnected = wifiConnected;

    lv_obj_t* prevFocused = (isMainMenuActive && ui_wheel_group) ? lv_group_get_focused(ui_wheel_group) : nullptr;
    if (isMainMenuActive && ui_wheel_group) {
        lv_group_remove_all_objs(ui_wheel_group);
    }

    // Hide all items first
    for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
        lv_obj_add_flag(mainMenuBtns[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Layout visible items
    bool prevStillVisible = false;
    for (int slot = 0; slot < neededCount; ++slot) {
        int i = mapIdx[slot];
        int itemY = (neededCount == 4) ? (9 + slot * 42) : (8 + slot * 42);

        lv_obj_set_size(mainMenuBtns[i], 260, 36);
        lv_obj_set_pos(mainMenuBtns[i], 30, itemY);
        lv_obj_clear_flag(mainMenuBtns[i], LV_OBJ_FLAG_HIDDEN);

        if (isMainMenuActive && ui_wheel_group) {
            lv_group_add_obj(ui_wheel_group, mainMenuBtns[i]);
        }
        if (prevFocused == mainMenuBtns[i]) {
            prevStillVisible = true;
        }
    }

    if (main_menu_list) {
        if (neededCount == 4) {
            lv_obj_set_scrollbar_mode(main_menu_list, LV_SCROLLBAR_MODE_OFF);
        } else {
            lv_obj_set_scrollbar_mode(main_menu_list, LV_SCROLLBAR_MODE_AUTO);
        }
    }

    if (isMainMenuActive) {
        if (resetFocus || prevFocused == nullptr || !prevStillVisible) {
            if (main_menu_list) lv_obj_scroll_to_y(main_menu_list, 0, LV_ANIM_OFF);
            if (ui_wheel_group) lv_group_focus_obj(mainMenuBtns[0]);
            for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
                if (i == 0) lv_obj_clear_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
                else lv_obj_add_flag(mainMenuArrows[i], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            if (ui_wheel_group) lv_group_focus_obj(prevFocused);
        }
    }

    Serial.printf("[UI] Main Menu layout refreshed: %d visible items (BLE=%d, WiFi=%d)\n",
                  neededCount, bleConnected ? 1 : 0, wifiConnected ? 1 : 0);
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

    // Menu Buttons (All 6 items created as children of main_menu_list; sized & positioned dynamically)
    for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
        mainMenuBtns[i] = lv_btn_create(main_menu_list);
        lv_obj_set_size(mainMenuBtns[i], 260, 36);
        lv_obj_add_style(mainMenuBtns[i], &style_btn_default, 0);
        if (i == 5) {
            // Power Off button (index 5): styled like back/exit button (filled with red on focus)
            lv_obj_add_style(mainMenuBtns[i], &style_btn_back_focused, LV_STATE_FOCUSED);
            lv_obj_add_style(mainMenuBtns[i], &style_btn_back_focused, LV_STATE_FOCUS_KEY);
        } else {
            lv_obj_add_style(mainMenuBtns[i], &style_btn_focused, LV_STATE_FOCUSED);
            lv_obj_add_style(mainMenuBtns[i], &style_btn_focused, LV_STATE_FOCUS_KEY);
        }
        lv_obj_add_style(mainMenuBtns[i], &style_btn_pressed, LV_STATE_PRESSED);

        // Selection Arrow Indicator
        mainMenuArrows[i] = lv_label_create(mainMenuBtns[i]);
        if (i == 5) {
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
        lv_obj_set_style_text_color(mainMenuLabels_objs[i], (i == 5) ? UI_COLOR_TEXT_DIM : UI_COLOR_TEXT, 0);

        // Right-side icon glyph
        mainMenuIcons[i] = lv_label_create(mainMenuBtns[i]);
        lv_label_set_text(mainMenuIcons[i], mainMenuGlyphs[i]);
        if (i == 2) {
            lv_obj_set_style_text_font(mainMenuIcons[i], &ui_font_circle, 0);
            lv_obj_set_style_text_color(mainMenuIcons[i], UI_COLOR_TEXT_DIM, 0);
        } else if (i == 3) {
            lv_obj_set_style_text_font(mainMenuIcons[i], &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(mainMenuIcons[i], lv_color_hex(0x2196F3), 0);
        } else {
            lv_obj_set_style_text_font(mainMenuIcons[i], &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(mainMenuIcons[i], UI_COLOR_TEXT_DIM, 0);
        }
        lv_obj_align(mainMenuIcons[i], LV_ALIGN_RIGHT_MID, -12, 0);

        lv_obj_add_event_cb(mainMenuBtns[i], mainMenuBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)i);
    }

    // Apply dynamic initial layout based on BLE and Wi-Fi connectivity
    refreshMainMenuWirelessState(true);

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
