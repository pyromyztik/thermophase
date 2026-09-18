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
    "Temporal AA with motion reprojection & CAS sharpening. Heavy temporal accumulation provides silky noise reduction and sub-pixel resolution.",
    "Edge-preserving 3x3 spatial filter. Strips thermal noise & checkerboard dither while preserving pin-sharp boundaries and true thermal hot-spots.",
    "Return to settings menu"
};

static const char* scalingOptionLabels[] = {
    "Nearest Neighbor",
    "Bilinear",
    "Bicubic (Catmull-Rom)",
    "Lanczos-3",
    "TAAU (Temporal AA)",
    "Live Denoise",
    "Back to Settings"
};

static lv_obj_t* scalingNameLabels[SCALING_OPTIONS_COUNT];
static lv_obj_t* scalingCheckmarks[SCALING_OPTIONS_COUNT];
static int focusedScalingIdx = 0;

void updateScalingDesc(int idx) {
    focusedScalingIdx = idx;
    if (scaling_desc_label == nullptr || idx >= SCALING_OPTIONS_COUNT) return;
    ScalingMode current = getScalingModeSetting();
    bool denoiseOn = getLiveDenoiseSetting();

    if (idx < 5 && (ScalingMode)idx == current) {
        char descBuf[256];
        snprintf(descBuf, sizeof(descBuf), "This is the selected option.\n\n%s", scalingOptionDescs[idx]);
        lv_label_set_text(scaling_desc_label, descBuf);
    } else if (idx == 5) {
        char descBuf[256];
        snprintf(descBuf, sizeof(descBuf), "Live Denoise is currently %s.\n\n%s",
                 denoiseOn ? "ENABLED" : "DISABLED", scalingOptionDescs[idx]);
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
        if (idx == 6) {
            lv_obj_set_style_text_color(scalingNameLabels[6], lv_color_black(), 0);
            if (scalingArrows[6] != nullptr) {
                lv_obj_set_style_text_color(scalingArrows[6], lv_color_black(), 0);
            }
        } else if (scalingNameLabels[6] != nullptr) {
            lv_obj_set_style_text_color(scalingNameLabels[6], UI_COLOR_TEXT_DIM, 0);
        }

        updateScalingDesc(idx);
        if (idx == 0) {
            lv_obj_scroll_to_y(scaling_list, 0, LV_ANIM_ON);
        } else {
            lv_obj_scroll_to_view(btn, LV_ANIM_ON);
        }

    } else if (code == LV_EVENT_DEFOCUSED) {
        if (idx == 6 && scalingNameLabels[6] != nullptr) {
            lv_obj_set_style_text_color(scalingNameLabels[6], UI_COLOR_TEXT_DIM, 0);
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
        } else if (idx == 5) {
            // Toggle Live Denoise (Peak-Preserving Median)
            bool newDenoise = !getLiveDenoiseSetting();
            setLiveDenoiseSetting(newDenoise);
            updateScalingValues();

            // Flash badge animation
            if (scalingCheckmarks[5] != nullptr) {
                lv_anim_t a_val;
                lv_anim_init(&a_val);
                lv_anim_set_var(&a_val, scalingCheckmarks[5]);
                lv_anim_set_values(&a_val, 40, 255);
                lv_anim_set_time(&a_val, 150);
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

void updateScalingValues() {
    ScalingMode current = getScalingModeSetting();
    bool denoiseOn = getLiveDenoiseSetting();

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

    // Live Denoise Button (Index 5)
    if (scalingBtns[5] != nullptr && scalingNameLabels[5] != nullptr) {
        lv_obj_set_style_bg_color(scalingBtns[5], UI_COLOR_BG, 0);
        lv_obj_set_style_bg_opa(scalingBtns[5], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(scalingBtns[5], UI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(scalingBtns[5], 1, 0);
        lv_obj_set_style_text_color(scalingNameLabels[5], UI_COLOR_TEXT, 0);

        if (scalingCheckmarks[5] != nullptr) {
            lv_label_set_text(scalingCheckmarks[5], denoiseOn ? "[ON]" : "[OFF]");
            lv_obj_set_style_text_color(scalingCheckmarks[5], denoiseOn ? lv_color_hex(0x00E5FF) : UI_COLOR_TEXT_DIM, 0);
            lv_obj_clear_flag(scalingCheckmarks[5], LV_OBJ_FLAG_HIDDEN);
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

    // Top-right Battery Indicator (Percentage to the left of the icon)
    createBatteryIndicator(screen_scaling, true, LV_ALIGN_TOP_RIGHT, -14, 11);

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
    int curY = 2;

    // Header 1: SCALING MODES
    lv_obj_t* secHdr1 = lv_label_create(scaling_list);
    lv_label_set_text(secHdr1, "SCALING MODES");
    lv_obj_set_pos(secHdr1, 4, curY);
    lv_obj_set_style_text_color(secHdr1, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(secHdr1, &lv_font_montserrat_10, 0);
    curY += 15;

    // Scaling Modes (0..4)
    for (int i = 0; i < 5; ++i) {
        scalingBtns[i] = lv_btn_create(scaling_list);
        lv_obj_set_pos(scalingBtns[i], 2, curY);
        lv_obj_set_size(scalingBtns[i], 198, itemH);
        lv_obj_add_style(scalingBtns[i], &style_btn_default, 0);
        lv_obj_add_style(scalingBtns[i], &style_btn_focused, LV_STATE_FOCUSED);
        lv_obj_add_style(scalingBtns[i], &style_btn_focused, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(scalingBtns[i], &style_btn_pressed, LV_STATE_PRESSED);

        scalingArrows[i] = lv_label_create(scalingBtns[i]);
        lv_label_set_text(scalingArrows[i], LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(scalingArrows[i], UI_COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(scalingArrows[i], &lv_font_montserrat_12, 0);
        lv_obj_align(scalingArrows[i], LV_ALIGN_LEFT_MID, 0, 0);
        if (i != 0) lv_obj_add_flag(scalingArrows[i], LV_OBJ_FLAG_HIDDEN);

        scalingNameLabels[i] = lv_label_create(scalingBtns[i]);
        lv_label_set_text(scalingNameLabels[i], scalingOptionLabels[i]);
        lv_obj_align(scalingNameLabels[i], LV_ALIGN_LEFT_MID, 13, 0);
        lv_obj_set_style_text_font(scalingNameLabels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(scalingNameLabels[i], UI_COLOR_TEXT, 0);

        scalingCheckmarks[i] = lv_label_create(scalingBtns[i]);
        lv_obj_align(scalingCheckmarks[i], LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_style_text_color(scalingCheckmarks[i], lv_color_hex(0x008C00), 0);
        lv_obj_set_style_text_font(scalingCheckmarks[i], &lv_font_montserrat_12, 0);
        lv_label_set_text(scalingCheckmarks[i], "");
        lv_obj_add_flag(scalingCheckmarks[i], LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_event_cb(scalingBtns[i], scalingBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)i);
        curY += itemH + spacing;
    }

    // Divider Line separating scaling modes from live denoise
    curY += 8;
    static lv_point_t sep1_pts[] = {{4, 0}, {196, 0}};
    lv_obj_t* sep1 = lv_line_create(scaling_list);
    lv_line_set_points(sep1, sep1_pts, 2);
    lv_obj_add_style(sep1, &style_divider, 0);
    lv_obj_set_pos(sep1, 0, curY);
    curY += 12;

    // Header 2: LIVE FILTERING
    lv_obj_t* secHdr2 = lv_label_create(scaling_list);
    lv_label_set_text(secHdr2, "LIVE FILTERING");
    lv_obj_set_pos(secHdr2, 4, curY);
    lv_obj_set_style_text_color(secHdr2, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(secHdr2, &lv_font_montserrat_10, 0);
    curY += 16;

    // Button 5: Live Denoise (Toggle)
    scalingBtns[5] = lv_btn_create(scaling_list);
    lv_obj_set_pos(scalingBtns[5], 2, curY);
    lv_obj_set_size(scalingBtns[5], 198, itemH);
    lv_obj_add_style(scalingBtns[5], &style_btn_default, 0);
    lv_obj_add_style(scalingBtns[5], &style_btn_focused, LV_STATE_FOCUSED);
    lv_obj_add_style(scalingBtns[5], &style_btn_focused, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(scalingBtns[5], &style_btn_pressed, LV_STATE_PRESSED);

    scalingArrows[5] = lv_label_create(scalingBtns[5]);
    lv_label_set_text(scalingArrows[5], LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(scalingArrows[5], UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(scalingArrows[5], &lv_font_montserrat_12, 0);
    lv_obj_align(scalingArrows[5], LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(scalingArrows[5], LV_OBJ_FLAG_HIDDEN);

    scalingNameLabels[5] = lv_label_create(scalingBtns[5]);
    lv_label_set_text(scalingNameLabels[5], scalingOptionLabels[5]);
    lv_obj_align(scalingNameLabels[5], LV_ALIGN_LEFT_MID, 13, 0);
    lv_obj_set_style_text_font(scalingNameLabels[5], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(scalingNameLabels[5], UI_COLOR_TEXT, 0);

    scalingCheckmarks[5] = lv_label_create(scalingBtns[5]);
    lv_obj_align(scalingCheckmarks[5], LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_text_font(scalingCheckmarks[5], &lv_font_montserrat_12, 0);
    lv_label_set_text(scalingCheckmarks[5], "[ON]");
    lv_obj_set_style_text_color(scalingCheckmarks[5], lv_color_hex(0x00E5FF), 0);

    lv_obj_add_event_cb(scalingBtns[5], scalingBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)5);
    curY += itemH + spacing;

    // Divider Line before Back button
    curY += 8;
    static lv_point_t sep2_pts[] = {{4, 0}, {196, 0}};
    lv_obj_t* sep2 = lv_line_create(scaling_list);
    lv_line_set_points(sep2, sep2_pts, 2);
    lv_obj_add_style(sep2, &style_divider, 0);
    lv_obj_set_pos(sep2, 0, curY);
    curY += 12;

    // Button 6: Back to Settings
    scalingBtns[6] = lv_btn_create(scaling_list);
    lv_obj_set_pos(scalingBtns[6], 2, curY);
    lv_obj_set_size(scalingBtns[6], 198, itemH);
    lv_obj_add_style(scalingBtns[6], &style_btn_default, 0);
    lv_obj_add_style(scalingBtns[6], &style_btn_back_focused, LV_STATE_FOCUSED);
    lv_obj_add_style(scalingBtns[6], &style_btn_back_focused, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(scalingBtns[6], &style_btn_pressed, LV_STATE_PRESSED);

    scalingArrows[6] = lv_label_create(scalingBtns[6]);
    lv_label_set_text(scalingArrows[6], LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(scalingArrows[6], lv_color_black(), 0);
    lv_obj_set_style_text_font(scalingArrows[6], &lv_font_montserrat_12, 0);
    lv_obj_align(scalingArrows[6], LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(scalingArrows[6], LV_OBJ_FLAG_HIDDEN);

    scalingNameLabels[6] = lv_label_create(scalingBtns[6]);
    lv_label_set_text(scalingNameLabels[6], scalingOptionLabels[6]);
    lv_obj_align(scalingNameLabels[6], LV_ALIGN_LEFT_MID, 13, 0);
    lv_obj_set_style_text_font(scalingNameLabels[6], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(scalingNameLabels[6], UI_COLOR_TEXT_DIM, 0);

    scalingCheckmarks[6] = nullptr;
    lv_obj_add_event_cb(scalingBtns[6], scalingBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)6);

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
