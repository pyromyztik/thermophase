#include "ui.hpp"
#include "display.hpp"

// Scaling Shared Variables
lv_obj_t* scalingBtns[SCALING_OPTIONS_COUNT];
lv_obj_t* scalingArrows[SCALING_OPTIONS_COUNT];
lv_obj_t* scaling_desc_label = nullptr;
lv_obj_t* scaling_list = nullptr;

const char* scalingOptionDescs[] = {
    "Blocky raw pixels; lowest CPU overhead & highest FPS",
    "Smooth 2x2 linear interpolation; fast performance",
    "4-tap Catmull-Rom spline; balanced sharpness & clarity",
    "6-tap Lanczos sinc filter; high edge reconstruction",
    "Temporal AA with motion reprojection & CAS sharpening. This is the heaviest scaling method - may reduce performance.",
    "Return to settings menu"
};

static const char* scalingOptionLabels[] = {
    "Nearest Neighbor",
    "Bilinear",
    "Bicubic (Catmull-Rom)",
    "Lanczos-3",
    "TAAU (Temporal AA)",
    "Back to Settings"
};

static lv_obj_t* scalingNameLabels[SCALING_OPTIONS_COUNT];
static lv_obj_t* scalingCheckmarks[SCALING_OPTIONS_COUNT];
static int focusedScalingIdx = 0;

void updateScalingDesc(int idx) {
    focusedScalingIdx = idx;
    if (scaling_desc_label == nullptr || idx >= SCALING_OPTIONS_COUNT) return;
    ScalingMode current = getScalingModeSetting();
    if (idx < 5 && (ScalingMode)idx == current) {
        char descBuf[256];
        snprintf(descBuf, sizeof(descBuf), "This is the selected option.\n\n%s", scalingOptionDescs[idx]);
        lv_label_set_text(scaling_desc_label, descBuf);
    } else {
        lv_label_set_text(scaling_desc_label, scalingOptionDescs[idx]);
    }
}

static void scalingBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_obj_t* btn = lv_event_get_target(e);
        focusedScalingIdx = idx;
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
            lv_obj_set_style_text_color(scalingNameLabels[5], UI_COLOR_TEXT_DIM, 0);
        }

        updateScalingDesc(idx);
        lv_obj_scroll_to_view(btn, LV_ANIM_ON);

    } else if (code == LV_EVENT_DEFOCUSED) {
        if (idx == 5 && scalingNameLabels[5] != nullptr) {
            lv_obj_set_style_text_color(scalingNameLabels[5], UI_COLOR_TEXT_DIM, 0);
        }
    } else if (code == LV_EVENT_CLICKED) {
        if (idx < 5) {
            ScalingMode chosen = (ScalingMode)idx;
            setScalingModeSetting(chosen);
            updateScalingMode(chosen);
            updateScalingValues();
            updateSettingsValues();

            // Flash checkmark animation
            if (scalingCheckmarks[idx] != nullptr) {
                lv_anim_t a_val;
                lv_anim_init(&a_val);
                lv_anim_set_var(&a_val, scalingCheckmarks[idx]);
                lv_anim_set_values(&a_val, 40, 255);
                lv_anim_set_time(&a_val, 150);
                lv_anim_set_exec_cb(&a_val, [](void* var, int32_t val) {
                    lv_obj_set_style_text_opa((lv_obj_t*)var, val, 0);
                });
                lv_anim_start(&a_val);
            }
        } else {
            // Back to Settings screen (focused on item 5 "Upscaling")
            uiNavToSettings(5);
        }
    }
}

void updateScalingValues() {
    ScalingMode current = getScalingModeSetting();
    for (int i = 0; i < 5; ++i) {
        if (scalingBtns[i] == nullptr || scalingNameLabels[i] == nullptr) continue;

        if ((ScalingMode)i == current) {
            // Selected option: White background, black text, green checkmark
            lv_obj_set_style_bg_color(scalingBtns[i], lv_color_white(), 0);
            lv_obj_set_style_bg_opa(scalingBtns[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(scalingBtns[i], lv_color_white(), 0);
            lv_obj_set_style_border_width(scalingBtns[i], 1, 0);
            lv_obj_set_style_text_color(scalingNameLabels[i], lv_color_black(), 0);

            if (scalingCheckmarks[i] != nullptr) {
                lv_label_set_text(scalingCheckmarks[i], LV_SYMBOL_OK);
                lv_obj_clear_flag(scalingCheckmarks[i], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            // Inactive option: Default dark background, white text, no checkmark
            lv_obj_set_style_bg_color(scalingBtns[i], UI_COLOR_BG, 0);
            lv_obj_set_style_bg_opa(scalingBtns[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(scalingBtns[i], UI_COLOR_BORDER, 0);
            lv_obj_set_style_border_width(scalingBtns[i], 1, 0);
            lv_obj_set_style_text_color(scalingNameLabels[i], UI_COLOR_TEXT, 0);

            if (scalingCheckmarks[i] != nullptr) {
                lv_label_set_text(scalingCheckmarks[i], "");
                lv_obj_add_flag(scalingCheckmarks[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    updateScalingDesc(focusedScalingIdx);
}

void buildScalingScreen() {
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

    // Vertical Divider Line separating options from right-hand contextual hints
    static lv_point_t v_line_pts[] = {{216, 32}, {216, 216}};
    lv_obj_t* line_vert = lv_line_create(screen_scaling);
    lv_line_set_points(line_vert, v_line_pts, 2);
    lv_obj_add_style(line_vert, &style_divider, 0);

    // Scrollable list (Left side)
    scaling_list = lv_obj_create(screen_scaling);
    lv_obj_set_pos(scaling_list, 4, 34);
    lv_obj_set_size(scaling_list, 208, 180);
    lv_obj_set_style_bg_opa(scaling_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scaling_list, 0, 0);
    lv_obj_set_style_pad_all(scaling_list, 0, 0);
    lv_obj_set_scroll_dir(scaling_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scaling_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(scaling_list, UI_COLOR_FOCUS_GREY, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(scaling_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(scaling_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(scaling_list, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(scaling_list, 1, LV_PART_SCROLLBAR);

    int itemH = 26;
    int spacing = 4;
    for (int i = 0; i < SCALING_OPTIONS_COUNT; ++i) {
        scalingBtns[i] = lv_btn_create(scaling_list);
        lv_obj_set_pos(scalingBtns[i], 2, i * (itemH + spacing));
        lv_obj_set_size(scalingBtns[i], 198, itemH);
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
        lv_obj_align(scalingArrows[i], LV_ALIGN_LEFT_MID, 0, 0);
        if (i != 0) lv_obj_add_flag(scalingArrows[i], LV_OBJ_FLAG_HIDDEN);

        // Name
        scalingNameLabels[i] = lv_label_create(scalingBtns[i]);
        lv_label_set_text(scalingNameLabels[i], scalingOptionLabels[i]);
        lv_obj_align(scalingNameLabels[i], LV_ALIGN_LEFT_MID, 13, 0);
        lv_obj_set_style_text_font(scalingNameLabels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(scalingNameLabels[i], (i == 5) ? UI_COLOR_TEXT_DIM : UI_COLOR_TEXT, 0);

        // Green Checkmark indicator (Right inside edge of button)
        if (i < 5) {
            scalingCheckmarks[i] = lv_label_create(scalingBtns[i]);
            lv_obj_align(scalingCheckmarks[i], LV_ALIGN_RIGHT_MID, -8, 0);
            lv_obj_set_style_text_color(scalingCheckmarks[i], lv_color_hex(0x008C00), 0);
            lv_obj_set_style_text_font(scalingCheckmarks[i], &lv_font_montserrat_12, 0);
            lv_label_set_text(scalingCheckmarks[i], "");
            lv_obj_add_flag(scalingCheckmarks[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            scalingCheckmarks[i] = nullptr;
        }

        lv_obj_add_event_cb(scalingBtns[i], scalingBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)i);
    }
    updateScalingValues();

    // Right-Side Contextual Description Panel
    lv_obj_t* hint_hdr = lv_label_create(screen_scaling);
    lv_label_set_text(hint_hdr, "INFO");
    lv_obj_set_pos(hint_hdr, 222, 38);
    lv_obj_set_style_text_color(hint_hdr, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint_hdr, &lv_font_montserrat_10, 0);

    scaling_desc_label = lv_label_create(screen_scaling);
    lv_obj_set_pos(scaling_desc_label, 222, 54);
    lv_obj_set_size(scaling_desc_label, 92, 150);
    lv_label_set_long_mode(scaling_desc_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(scaling_desc_label, UI_COLOR_TEXT_DIM, 0);
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
