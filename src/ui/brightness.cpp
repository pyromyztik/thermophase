#include "ui.hpp"
#include "display.hpp"

// Brightness Screen UI Elements
static lv_obj_t* brightness_card = nullptr;
static lv_obj_t* brightness_slider = nullptr;
static lv_obj_t* brightness_name_label = nullptr;
static lv_obj_t* brightness_val_label = nullptr;
static lv_obj_t* brightness_status_label = nullptr;
static lv_obj_t* brightness_back_btn = nullptr;
static lv_obj_t* brightness_back_label = nullptr;
static lv_obj_t* slider_arrow = nullptr;
static lv_obj_t* back_arrow = nullptr;
static lv_obj_t* brightness_desc_label = nullptr;
static lv_obj_t* brightness_footer_label = nullptr;

static void updateSliderVisualState(bool isFocused, bool isEditing) {
    if (!brightness_card) return;

    if (isEditing) {
        lv_obj_set_style_border_color(brightness_card, UI_COLOR_YELLOW, 0);
        lv_obj_set_style_border_width(brightness_card, 2, 0);
        lv_obj_set_style_outline_color(brightness_card, UI_COLOR_YELLOW, 0);
        lv_obj_set_style_outline_width(brightness_card, 1, 0);
        lv_obj_set_style_outline_opa(brightness_card, LV_OPA_COVER, 0);

        if (slider_arrow) {
            lv_obj_clear_flag(slider_arrow, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(slider_arrow, UI_COLOR_YELLOW, 0);
        }
        if (brightness_status_label) {
            lv_label_set_text(brightness_status_label, "[Rotate wheel to set, Click to lock]");
            lv_obj_set_style_text_color(brightness_status_label, UI_COLOR_YELLOW, 0);
        }
        if (brightness_desc_label) {
            lv_label_set_text(brightness_desc_label, "Rotate wheel to adjust brightness (10% - 100%). Click to set.");
        }
        if (brightness_footer_label) {
            lv_label_set_text(brightness_footer_label, "[Rotate] Change Level   |   [Click] Lock & Set");
        }
    } else if (isFocused) {
        lv_obj_set_style_border_color(brightness_card, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_border_width(brightness_card, 2, 0);
        lv_obj_set_style_outline_color(brightness_card, UI_COLOR_FOCUS_GREY, 0);
        lv_obj_set_style_outline_width(brightness_card, 2, 0);
        lv_obj_set_style_outline_opa(brightness_card, LV_OPA_COVER, 0);

        if (slider_arrow) {
            lv_obj_clear_flag(slider_arrow, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(slider_arrow, UI_COLOR_ACCENT, 0);
        }
        if (brightness_status_label) {
            lv_label_set_text(brightness_status_label, "[Click knob to enter adjust mode]");
            lv_obj_set_style_text_color(brightness_status_label, UI_COLOR_TEXT_DIM, 0);
        }
        if (brightness_desc_label) {
            lv_label_set_text(brightness_desc_label, "Select slider to adjust or rotate to Back.");
        }
        if (brightness_footer_label) {
            lv_label_set_text(brightness_footer_label, "[Rotate] Navigate   |   [Click] Select / Adjust");
        }
    } else {
        lv_obj_set_style_border_color(brightness_card, UI_COLOR_SEPARATOR, 0);
        lv_obj_set_style_border_width(brightness_card, 1, 0);
        lv_obj_set_style_outline_width(brightness_card, 0, 0);

        if (slider_arrow) {
            lv_obj_add_flag(slider_arrow, LV_OBJ_FLAG_HIDDEN);
        }
        if (brightness_status_label) {
            lv_label_set_text(brightness_status_label, "");
        }
    }
}

static void brightnessSliderEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* obj = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        bool editing = lv_group_get_editing(ui_wheel_group);
        updateSliderVisualState(true, editing);
        if (back_arrow) lv_obj_add_flag(back_arrow, LV_OBJ_FLAG_HIDDEN);
        if (brightness_back_label) lv_obj_set_style_text_color(brightness_back_label, UI_COLOR_TEXT_DIM, 0);

    } else if (code == LV_EVENT_DEFOCUSED) {
        updateSliderVisualState(false, false);

    } else if (code == LV_EVENT_VALUE_CHANGED) {
        int rawVal = lv_slider_get_value(obj);
        int percent = rawVal * 5; // 2..20 -> 10%..100%
        if (percent < 10) percent = 10;
        if (percent > 100) percent = 100;

        setDisplayBrightnessSetting((uint8_t)percent);

        if (brightness_val_label) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%", percent);
            lv_label_set_text(brightness_val_label, buf);
        }
        updateSettingsValues();

    } else if (code == LV_EVENT_CLICKED) {
        // When clicked while editing, LVGL automatically clears editing mode.
        // We ensure fail-safe behavior and provide visual confirmation flash.
        bool editing = lv_group_get_editing(ui_wheel_group);
        if (editing) {
            lv_group_set_editing(ui_wheel_group, false);
        }
        updateSliderVisualState(true, false);

        if (brightness_val_label) {
            lv_anim_t a_val;
            lv_anim_init(&a_val);
            lv_anim_set_var(&a_val, brightness_val_label);
            lv_anim_set_values(&a_val, 255, 60);
            lv_anim_set_time(&a_val, 120);
            lv_anim_set_playback_time(&a_val, 120);
            lv_anim_set_exec_cb(&a_val, [](void* var, int32_t val) {
                lv_obj_set_style_text_opa((lv_obj_t*)var, val, 0);
            });
            lv_anim_start(&a_val);
        }
    }
}

static void brightnessBackBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        updateSliderVisualState(false, false);
        if (back_arrow) {
            lv_obj_clear_flag(back_arrow, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(back_arrow, lv_color_black(), 0);
        }
        if (slider_arrow) lv_obj_add_flag(slider_arrow, LV_OBJ_FLAG_HIDDEN);
        if (brightness_back_label) lv_obj_set_style_text_color(brightness_back_label, lv_color_black(), 0);
        if (brightness_desc_label) lv_label_set_text(brightness_desc_label, "Return to settings menu.");
        if (brightness_footer_label) lv_label_set_text(brightness_footer_label, "[Rotate] Navigate   |   [Click] Back to Settings");

    } else if (code == LV_EVENT_DEFOCUSED) {
        if (back_arrow) lv_obj_add_flag(back_arrow, LV_OBJ_FLAG_HIDDEN);
        if (brightness_back_label) lv_obj_set_style_text_color(brightness_back_label, UI_COLOR_TEXT_DIM, 0);

    } else if (code == LV_EVENT_CLICKED) {
        uiNavToSettings(SETTING_BRIGHTNESS); // Return to settings focused on Brightness
    }
}

void updateBrightnessValues() {
    uint8_t cur = getDisplayBrightnessSetting();
    if (cur < 10) cur = 10;
    if (cur > 100) cur = 100;

    if (brightness_slider != nullptr) {
        int sliderVal = (cur + 2) / 5; // Round to nearest 5%
        if (sliderVal < 2) sliderVal = 2;
        if (sliderVal > 20) sliderVal = 20;
        lv_slider_set_value(brightness_slider, sliderVal, LV_ANIM_OFF);
    }
    if (brightness_val_label != nullptr) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", cur);
        lv_label_set_text(brightness_val_label, buf);
    }
}

void setupBrightnessGroup() {
    if (brightness_slider != nullptr) {
        lv_group_add_obj(ui_wheel_group, brightness_slider);
    }
    if (brightness_back_btn != nullptr) {
        lv_group_add_obj(ui_wheel_group, brightness_back_btn);
    }

    // Default focus on slider
    if (brightness_slider != nullptr) {
        lv_group_focus_obj(brightness_slider);
        updateSliderVisualState(true, false);
    }
    if (slider_arrow != nullptr) lv_obj_clear_flag(slider_arrow, LV_OBJ_FLAG_HIDDEN);
    if (back_arrow != nullptr) lv_obj_add_flag(back_arrow, LV_OBJ_FLAG_HIDDEN);

    updateBrightnessValues();
}

void buildBrightnessScreen() {
    screen_brightness = lv_obj_create(NULL);
    lv_obj_add_style(screen_brightness, &style_screen, 0);
    lv_obj_clear_flag(screen_brightness, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_brightness, LV_SCROLLBAR_MODE_OFF);

    // 1. Header Title
    lv_obj_t* header = lv_label_create(screen_brightness);
    lv_label_set_text(header, "DISPLAY BRIGHTNESS");
    lv_obj_add_style(header, &style_header_title, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 14, 9);

    // 2. Header Divider Line
    static lv_point_t h_line_pts[] = {{0, 32}, {DISPLAY_WIDTH - 1, 32}};
    lv_obj_t* line_top = lv_line_create(screen_brightness);
    lv_line_set_points(line_top, h_line_pts, 2);
    lv_obj_add_style(line_top, &style_divider, 0);

    // Top-right Battery Indicator (Percentage to the left of the icon)
    createBatteryIndicator(screen_brightness, true, LV_ALIGN_TOP_RIGHT, -14, 11);

    // Vertical Divider Line separating options from right-hand contextual hints
    static lv_point_t v_line_pts[] = {{216, 32}, {216, 216}};
    lv_obj_t* line_vert = lv_line_create(screen_brightness);
    lv_line_set_points(line_vert, v_line_pts, 2);
    lv_obj_add_style(line_vert, &style_divider, 0);

    // 3. Slider Card Container (Left side)
    brightness_card = lv_obj_create(screen_brightness);
    lv_obj_set_pos(brightness_card, 6, 36);
    lv_obj_set_size(brightness_card, 198, 114);
    lv_obj_set_style_bg_color(brightness_card, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(brightness_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(brightness_card, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(brightness_card, 1, 0);
    lv_obj_set_style_radius(brightness_card, 0, 0);
    lv_obj_set_style_pad_hor(brightness_card, 12, 0);
    lv_obj_set_style_pad_ver(brightness_card, 10, 0);
    lv_obj_clear_flag(brightness_card, LV_OBJ_FLAG_SCROLLABLE);

    // Arrow indicator inside card (top-left)
    slider_arrow = lv_label_create(brightness_card);
    lv_label_set_text(slider_arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(slider_arrow, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(slider_arrow, &lv_font_montserrat_12, 0);
    lv_obj_align(slider_arrow, LV_ALIGN_TOP_LEFT, 0, 1);

    // Title inside card (top-left, offset 13px)
    brightness_name_label = lv_label_create(brightness_card);
    lv_label_set_text(brightness_name_label, "Backlight Level");
    lv_obj_align(brightness_name_label, LV_ALIGN_TOP_LEFT, 13, 0);
    lv_obj_set_style_text_font(brightness_name_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(brightness_name_label, UI_COLOR_TEXT, 0);

    // Percentage value readout (top-right of card)
    brightness_val_label = lv_label_create(brightness_card);
    lv_label_set_text(brightness_val_label, "80%");
    lv_obj_align(brightness_val_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_text_font(brightness_val_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(brightness_val_label, UI_COLOR_ACCENT, 0);

    // Actual LVGL Slider Widget
    brightness_slider = lv_slider_create(brightness_card);
    lv_obj_set_pos(brightness_slider, 8, 28);
    lv_obj_set_size(brightness_slider, 158, 14);
    // Range 2..20 maps to 10%..100% in convenient 5% steps
    lv_slider_set_range(brightness_slider, 2, 20);
    lv_slider_set_value(brightness_slider, 16, LV_ANIM_OFF);

    // Style the slider track (MAIN)
    // pad_hor = 0 ensures full left-to-right fill; pad_ver = 1 insets fill from 1px border so it never spills over bottom
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(brightness_slider, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_border_width(brightness_slider, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(brightness_slider, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(brightness_slider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(brightness_slider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(brightness_slider, 1, LV_PART_MAIN);

    // Style the filled track (INDICATOR)
    lv_obj_set_style_bg_color(brightness_slider, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(brightness_slider, 0, LV_PART_INDICATOR);

    // Style the knob (KNOB)
    // 18px wide (pad_hor = 2 with 14px track), 22px tall (pad_ver = 4 with 14px track)
    // Overhangs track edges by 9px while staying symmetrically bounded inside the card
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(brightness_slider, UI_COLOR_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_border_width(brightness_slider, 2, LV_PART_KNOB);
    lv_obj_set_style_radius(brightness_slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_ver(brightness_slider, 4, LV_PART_KNOB);
    lv_obj_set_style_pad_hor(brightness_slider, 2, LV_PART_KNOB);

    // Edited state for knob & indicator
    lv_obj_set_style_bg_color(brightness_slider, UI_COLOR_YELLOW, LV_PART_KNOB | LV_STATE_EDITED);
    lv_obj_set_style_border_color(brightness_slider, UI_COLOR_YELLOW, LV_PART_KNOB | LV_STATE_EDITED);
    lv_obj_set_style_bg_color(brightness_slider, UI_COLOR_YELLOW, LV_PART_INDICATOR | LV_STATE_EDITED);

    lv_obj_add_event_cb(brightness_slider, brightnessSliderEventCb, LV_EVENT_ALL, NULL);

    // Status / hint text below slider inside card
    brightness_status_label = lv_label_create(brightness_card);
    lv_label_set_text(brightness_status_label, "[Click knob to adjust]");
    lv_obj_set_pos(brightness_status_label, 0, 54);
    lv_obj_set_size(brightness_status_label, 174, 32);
    lv_label_set_long_mode(brightness_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(brightness_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(brightness_status_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(brightness_status_label, UI_COLOR_TEXT_DIM, 0);

    // 4. Back Button (below card)
    brightness_back_btn = lv_btn_create(screen_brightness);
    lv_obj_set_pos(brightness_back_btn, 6, 158);
    lv_obj_set_size(brightness_back_btn, 198, 26);
    lv_obj_add_style(brightness_back_btn, &style_btn_default, 0);
    lv_obj_add_style(brightness_back_btn, &style_btn_back_focused, LV_STATE_FOCUSED);
    lv_obj_add_style(brightness_back_btn, &style_btn_back_focused, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(brightness_back_btn, &style_btn_pressed, LV_STATE_PRESSED);

    back_arrow = lv_label_create(brightness_back_btn);
    lv_label_set_text(back_arrow, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_arrow, lv_color_black(), 0);
    lv_obj_set_style_text_font(back_arrow, &lv_font_montserrat_12, 0);
    lv_obj_align(back_arrow, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(back_arrow, LV_OBJ_FLAG_HIDDEN);

    brightness_back_label = lv_label_create(brightness_back_btn);
    lv_label_set_text(brightness_back_label, "Back to Settings");
    lv_obj_align(brightness_back_label, LV_ALIGN_LEFT_MID, 13, 0);
    lv_obj_set_style_text_font(brightness_back_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(brightness_back_label, UI_COLOR_TEXT_DIM, 0);

    lv_obj_add_event_cb(brightness_back_btn, brightnessBackBtnEventCb, LV_EVENT_ALL, NULL);

    // Right-Side Contextual Description Panel
    lv_obj_t* hint_hdr = lv_label_create(screen_brightness);
    lv_label_set_text(hint_hdr, "INFO");
    lv_obj_set_pos(hint_hdr, 222, 38);
    lv_obj_set_style_text_color(hint_hdr, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint_hdr, &lv_font_montserrat_10, 0);

    brightness_desc_label = lv_label_create(screen_brightness);
    lv_obj_set_pos(brightness_desc_label, 222, 54);
    lv_obj_set_size(brightness_desc_label, 92, 150);
    lv_label_set_long_mode(brightness_desc_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(brightness_desc_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(brightness_desc_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(brightness_desc_label, "Select slider to adjust or rotate to Back.");

    // Footer Divider Line
    static lv_point_t f_line_pts[] = {{0, 216}, {DISPLAY_WIDTH - 1, 216}};
    lv_obj_t* line_bot = lv_line_create(screen_brightness);
    lv_line_set_points(line_bot, f_line_pts, 2);
    lv_obj_add_style(line_bot, &style_divider, 0);

    // Footer Navigation Hints
    brightness_footer_label = lv_label_create(screen_brightness);
    lv_label_set_text(brightness_footer_label, "[Rotate] Navigate   |   [Click] Select / Adjust");
    lv_obj_add_style(brightness_footer_label, &style_footer_text, 0);
    lv_obj_align(brightness_footer_label, LV_ALIGN_BOTTOM_LEFT, 14, -8);
}

