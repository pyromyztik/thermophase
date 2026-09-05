#include "ui.hpp"
#include "power.hpp"

// Main Menu Shared Variables
lv_obj_t* mainMenuBtns[MAIN_MENU_COUNT];
lv_obj_t* mainMenuArrows[MAIN_MENU_COUNT];

static const char* mainMenuLabels[] = {
    "Live Thermal View",
    "Settings",
    "Power Off"
};
static int currentMenuIndex = 0;

static void mainMenuBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
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
