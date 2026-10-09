#include "ui.hpp"
#include "display.hpp"
#include <cmath>

// Emissivity Preset Structure
struct EmissivityPreset {
    const char* name;
    float value;
    const char* desc;
};

static const EmissivityPreset PRESETS[] = {
    {"Matte / Standard", 0.95f, "Matte surfaces: Plastics, rubber, electrical tape, paint, wood, drywall, water (e = 0.95)."},
    {"Human Skin",       0.98f, "Biological tissue, human skin screening & medical thermography (e = 0.98)."},
    {"Wood / Ceramic",   0.90f, "Wood, paper, unglazed ceramics, masonry, concrete, soil (e = 0.90)."},
    {"Oxidized Metal",   0.80f, "Heavily oxidized steel, rusty iron, dark anodized aluminum (e = 0.80)."},
    {"Cast Iron / Steel",0.60f, "Cast iron, rough turned steel, ground metals (e = 0.60)."},
    {"Shiny / Polished", 0.20f, "Polished aluminum, copper, brass, stainless steel, chrome (e = 0.20)."}
};
constexpr int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

// UI Element Handles
static lv_obj_t* emissivity_list = nullptr;
static lv_obj_t* emissivity_card = nullptr;
static lv_obj_t* emissivity_slider = nullptr;
static lv_obj_t* emissivity_name_label = nullptr;
static lv_obj_t* emissivity_val_label = nullptr;
static lv_obj_t* emissivity_status_label = nullptr;
static lv_obj_t* slider_arrow = nullptr;

static lv_obj_t* presetBtns[PRESET_COUNT];
static lv_obj_t* presetArrows[PRESET_COUNT];
static lv_obj_t* presetNameLabels[PRESET_COUNT];
static lv_obj_t* presetValLabels[PRESET_COUNT];

static lv_obj_t* emissivity_back_btn = nullptr;
static lv_obj_t* emissivity_back_label = nullptr;
static lv_obj_t* back_arrow = nullptr;

static lv_obj_t* emissivity_desc_label = nullptr;
static lv_obj_t* emissivity_footer_label = nullptr;

static void updateSliderVisualState(bool isFocused, bool isEditing) {
    if (!emissivity_card) return;

    if (isEditing) {
        lv_obj_set_style_border_color(emissivity_card, UI_COLOR_YELLOW, 0);
        lv_obj_set_style_border_width(emissivity_card, 2, 0);
        lv_obj_set_style_outline_color(emissivity_card, UI_COLOR_YELLOW, 0);
        lv_obj_set_style_outline_width(emissivity_card, 1, 0);
        lv_obj_set_style_outline_opa(emissivity_card, LV_OPA_COVER, 0);

        if (slider_arrow) {
            lv_obj_clear_flag(slider_arrow, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(slider_arrow, UI_COLOR_YELLOW, 0);
        }
        if (emissivity_status_label) {
            lv_label_set_text(emissivity_status_label, "[Rotate wheel to set, Click to lock]");
            lv_obj_set_style_text_color(emissivity_status_label, UI_COLOR_YELLOW, 0);
        }
        if (emissivity_desc_label) {
            lv_label_set_text(emissivity_desc_label, "Rotate wheel to adjust emissivity (0.10 to 1.00). Click to lock.");
        }
        if (emissivity_footer_label) {
            lv_label_set_text(emissivity_footer_label, "[Rotate] Change Level   |   [Click] Lock & Set");
        }
    } else if (isFocused) {
        lv_obj_set_style_border_color(emissivity_card, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_border_width(emissivity_card, 2, 0);
        lv_obj_set_style_outline_color(emissivity_card, UI_COLOR_FOCUS_GREY, 0);
        lv_obj_set_style_outline_width(emissivity_card, 2, 0);
        lv_obj_set_style_outline_opa(emissivity_card, LV_OPA_COVER, 0);

        if (slider_arrow) {
            lv_obj_clear_flag(slider_arrow, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(slider_arrow, UI_COLOR_ACCENT, 0);
        }
        if (emissivity_status_label) {
            lv_label_set_text(emissivity_status_label, "[Click knob to enter adjust mode]");
            lv_obj_set_style_text_color(emissivity_status_label, UI_COLOR_TEXT_DIM, 0);
        }
        if (emissivity_desc_label) {
            lv_label_set_text(emissivity_desc_label, "Fine-tune surface emissivity from 0.10 to 1.00. Click to adjust.");
        }
        if (emissivity_footer_label) {
            lv_label_set_text(emissivity_footer_label, "[Rotate] Navigate   |   [Click] Select / Adjust");
        }
    } else {
        lv_obj_set_style_border_color(emissivity_card, UI_COLOR_SEPARATOR, 0);
        lv_obj_set_style_border_width(emissivity_card, 1, 0);
        lv_obj_set_style_outline_width(emissivity_card, 0, 0);

        if (slider_arrow) {
            lv_obj_add_flag(slider_arrow, LV_OBJ_FLAG_HIDDEN);
        }
        if (emissivity_status_label) {
            lv_label_set_text(emissivity_status_label, "");
        }
    }
}

static void emissivitySliderEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* obj = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        bool editing = lv_group_get_editing(ui_wheel_group);
        updateSliderVisualState(true, editing);
        if (back_arrow) lv_obj_add_flag(back_arrow, LV_OBJ_FLAG_HIDDEN);
        if (emissivity_back_label) lv_obj_set_style_text_color(emissivity_back_label, UI_COLOR_TEXT_DIM, 0);
        for (int i = 0; i < PRESET_COUNT; ++i) {
            if (presetArrows[i]) lv_obj_add_flag(presetArrows[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (emissivity_list) lv_obj_scroll_to_y(emissivity_list, 0, LV_ANIM_ON);

    } else if (code == LV_EVENT_DEFOCUSED) {
        updateSliderVisualState(false, false);

    } else if (code == LV_EVENT_VALUE_CHANGED) {
        int rawVal = lv_slider_get_value(obj);
        float emi = rawVal / 100.0f;
        if (emi < 0.10f) emi = 0.10f;
        if (emi > 1.00f) emi = 1.00f;

        setEmissivitySetting(emi);

        if (emissivity_val_label) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.2f", emi);
            lv_label_set_text(emissivity_val_label, buf);
        }
        updateSettingsValues();

    } else if (code == LV_EVENT_CLICKED) {
        bool editing = lv_group_get_editing(ui_wheel_group);
        if (editing) {
            lv_group_set_editing(ui_wheel_group, false);
        }
        updateSliderVisualState(true, false);

        if (emissivity_val_label) {
            lv_anim_t a_val;
            lv_anim_init(&a_val);
            lv_anim_set_var(&a_val, emissivity_val_label);
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

static void presetBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_FOCUSED) {
        updateSliderVisualState(false, false);
        for (int i = 0; i < PRESET_COUNT; ++i) {
            if (i == idx) {
                if (presetArrows[i]) lv_obj_clear_flag(presetArrows[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                if (presetArrows[i]) lv_obj_add_flag(presetArrows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (back_arrow) lv_obj_add_flag(back_arrow, LV_OBJ_FLAG_HIDDEN);
        if (emissivity_back_label) lv_obj_set_style_text_color(emissivity_back_label, UI_COLOR_TEXT_DIM, 0);

        if (emissivity_desc_label && idx >= 0 && idx < PRESET_COUNT) {
            lv_label_set_text(emissivity_desc_label, PRESETS[idx].desc);
        }
        if (emissivity_footer_label) {
            lv_label_set_text(emissivity_footer_label, "[Rotate] Navigate   |   [Click] Apply Preset");
        }
        if (emissivity_list && idx >= 0 && idx < PRESET_COUNT) {
            lv_obj_scroll_to_view(presetBtns[idx], LV_ANIM_ON);
        }

    } else if (code == LV_EVENT_DEFOCUSED) {
        if (idx >= 0 && idx < PRESET_COUNT && presetArrows[idx]) {
            lv_obj_add_flag(presetArrows[idx], LV_OBJ_FLAG_HIDDEN);
        }

    } else if (code == LV_EVENT_CLICKED) {
        if (idx >= 0 && idx < PRESET_COUNT) {
            float val = PRESETS[idx].value;
            setEmissivitySetting(val);

            // Update slider position & label
            if (emissivity_slider) {
                int sliderVal = (int)(val * 100.0f + 0.5f);
                lv_slider_set_value(emissivity_slider, sliderVal, LV_ANIM_ON);
            }
            if (emissivity_val_label) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.2f", val);
                lv_label_set_text(emissivity_val_label, buf);

                // Flash animation
                lv_anim_t a_val;
                lv_anim_init(&a_val);
                lv_anim_set_var(&a_val, emissivity_val_label);
                lv_anim_set_values(&a_val, 255, 60);
                lv_anim_set_time(&a_val, 120);
                lv_anim_set_playback_time(&a_val, 120);
                lv_anim_set_exec_cb(&a_val, [](void* var, int32_t val) {
                    lv_obj_set_style_text_opa((lv_obj_t*)var, val, 0);
                });
                lv_anim_start(&a_val);
            }

            // Flash preset value label
            if (presetValLabels[idx]) {
                lv_anim_t a_p;
                lv_anim_init(&a_p);
                lv_anim_set_var(&a_p, presetValLabels[idx]);
                lv_anim_set_values(&a_p, 255, 60);
                lv_anim_set_time(&a_p, 120);
                lv_anim_set_playback_time(&a_p, 120);
                lv_anim_set_exec_cb(&a_p, [](void* var, int32_t val) {
                    lv_obj_set_style_text_opa((lv_obj_t*)var, val, 0);
                });
                lv_anim_start(&a_p);
            }

            updateSettingsValues();
        }
    }
}

static void emissivityBackBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        updateSliderVisualState(false, false);
        for (int i = 0; i < PRESET_COUNT; ++i) {
            if (presetArrows[i]) lv_obj_add_flag(presetArrows[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (back_arrow) {
            lv_obj_clear_flag(back_arrow, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(back_arrow, lv_color_black(), 0);
        }
        if (slider_arrow) lv_obj_add_flag(slider_arrow, LV_OBJ_FLAG_HIDDEN);
        if (emissivity_back_label) lv_obj_set_style_text_color(emissivity_back_label, lv_color_black(), 0);
        if (emissivity_desc_label) lv_label_set_text(emissivity_desc_label, "Return to settings menu.");
        if (emissivity_footer_label) lv_label_set_text(emissivity_footer_label, "[Rotate] Navigate   |   [Click] Back to Settings");
        if (emissivity_list && emissivity_back_btn) {
            lv_obj_scroll_to_view(emissivity_back_btn, LV_ANIM_ON);
        }

    } else if (code == LV_EVENT_DEFOCUSED) {
        if (back_arrow) lv_obj_add_flag(back_arrow, LV_OBJ_FLAG_HIDDEN);
        if (emissivity_back_label) lv_obj_set_style_text_color(emissivity_back_label, UI_COLOR_TEXT_DIM, 0);

    } else if (code == LV_EVENT_CLICKED) {
        uiNavToSettings(SETTING_EMISSIVITY);
    }
}

void updateEmissivityValues() {
    float cur = getEmissivitySetting();
    if (cur < 0.10f) cur = 0.10f;
    if (cur > 1.00f) cur = 1.00f;

    if (emissivity_slider != nullptr) {
        int sliderVal = (int)(cur * 100.0f + 0.5f);
        if (sliderVal < 10) sliderVal = 10;
        if (sliderVal > 100) sliderVal = 100;
        lv_slider_set_value(emissivity_slider, sliderVal, LV_ANIM_OFF);
    }
    if (emissivity_val_label != nullptr) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", cur);
        lv_label_set_text(emissivity_val_label, buf);
    }
}

void setupEmissivityGroup() {
    if (emissivity_slider != nullptr) {
        lv_group_add_obj(ui_wheel_group, emissivity_slider);
    }
    for (int i = 0; i < PRESET_COUNT; ++i) {
        if (presetBtns[i] != nullptr) {
            lv_group_add_obj(ui_wheel_group, presetBtns[i]);
        }
    }
    if (emissivity_back_btn != nullptr) {
        lv_group_add_obj(ui_wheel_group, emissivity_back_btn);
    }

    // Default focus on slider
    if (emissivity_slider != nullptr) {
        lv_group_focus_obj(emissivity_slider);
        updateSliderVisualState(true, false);
    }
    if (slider_arrow != nullptr) lv_obj_clear_flag(slider_arrow, LV_OBJ_FLAG_HIDDEN);
    if (back_arrow != nullptr) lv_obj_add_flag(back_arrow, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < PRESET_COUNT; ++i) {
        if (presetArrows[i] != nullptr) lv_obj_add_flag(presetArrows[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (emissivity_list != nullptr) {
        lv_obj_scroll_to_y(emissivity_list, 0, LV_ANIM_OFF);
    }

    updateEmissivityValues();
}

void buildEmissivityScreen() {
    screen_emissivity = lv_obj_create(NULL);
    lv_obj_add_style(screen_emissivity, &style_screen, 0);
    lv_obj_clear_flag(screen_emissivity, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_emissivity, LV_SCROLLBAR_MODE_OFF);

    // 1. Header Title
    lv_obj_t* header = lv_label_create(screen_emissivity);
    lv_label_set_text(header, "SURFACE EMISSIVITY");
    lv_obj_add_style(header, &style_header_title, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 14, 9);

    // 2. Header Divider Line
    static lv_point_t h_line_pts[] = {{0, 32}, {DISPLAY_WIDTH - 1, 32}};
    lv_obj_t* line_top = lv_line_create(screen_emissivity);
    lv_line_set_points(line_top, h_line_pts, 2);
    lv_obj_add_style(line_top, &style_divider, 0);

    // Top-right Battery Indicator
    createBatteryIndicator(screen_emissivity, true, LV_ALIGN_TOP_RIGHT, -14, 11);

    // Vertical Divider Line separating options from right-hand contextual hints
    static lv_point_t v_line_pts[] = {{216, 32}, {216, 216}};
    lv_obj_t* line_vert = lv_line_create(screen_emissivity);
    lv_line_set_points(line_vert, v_line_pts, 2);
    lv_obj_add_style(line_vert, &style_divider, 0);

    // Scrollable Container (Left side)
    emissivity_list = lv_obj_create(screen_emissivity);
    lv_obj_set_pos(emissivity_list, 4, 34);
    lv_obj_set_size(emissivity_list, 208, 180);
    lv_obj_set_style_bg_opa(emissivity_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(emissivity_list, 0, 0);
    lv_obj_set_style_pad_all(emissivity_list, 0, 0);
    lv_obj_set_scroll_dir(emissivity_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(emissivity_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(emissivity_list, UI_COLOR_FOCUS_GREY, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(emissivity_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(emissivity_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(emissivity_list, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(emissivity_list, 1, LV_PART_SCROLLBAR);

    int curY = 2;
    int itemH = 26;
    int spacing = 3;

    // 3. Slider Card Container
    emissivity_card = lv_obj_create(emissivity_list);
    lv_obj_set_pos(emissivity_card, 2, curY);
    lv_obj_set_size(emissivity_card, 198, 92);
    lv_obj_set_style_bg_color(emissivity_card, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(emissivity_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(emissivity_card, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(emissivity_card, 1, 0);
    lv_obj_set_style_radius(emissivity_card, 0, 0);
    lv_obj_set_style_pad_hor(emissivity_card, 12, 0);
    lv_obj_set_style_pad_ver(emissivity_card, 8, 0);
    lv_obj_clear_flag(emissivity_card, LV_OBJ_FLAG_SCROLLABLE);

    // Arrow indicator inside card (top-left)
    slider_arrow = lv_label_create(emissivity_card);
    lv_label_set_text(slider_arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(slider_arrow, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(slider_arrow, &lv_font_montserrat_12, 0);
    lv_obj_align(slider_arrow, LV_ALIGN_TOP_LEFT, 0, 1);

    // Title inside card (top-left, offset 13px)
    emissivity_name_label = lv_label_create(emissivity_card);
    lv_label_set_text(emissivity_name_label, "Custom Level");
    lv_obj_align(emissivity_name_label, LV_ALIGN_TOP_LEFT, 13, 0);
    lv_obj_set_style_text_font(emissivity_name_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(emissivity_name_label, UI_COLOR_TEXT, 0);

    // Value readout (top-right of card)
    emissivity_val_label = lv_label_create(emissivity_card);
    lv_label_set_text(emissivity_val_label, "0.95");
    lv_obj_align(emissivity_val_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_text_font(emissivity_val_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(emissivity_val_label, UI_COLOR_ACCENT, 0);

    // Actual LVGL Slider Widget
    emissivity_slider = lv_slider_create(emissivity_card);
    lv_obj_set_pos(emissivity_slider, 8, 26);
    lv_obj_set_size(emissivity_slider, 158, 14);
    // Range 10..100 maps to 0.10..1.00 in 0.01 increments
    lv_slider_set_range(emissivity_slider, 10, 100);
    lv_slider_set_value(emissivity_slider, 95, LV_ANIM_OFF);

    // Slider track (MAIN)
    lv_obj_set_style_bg_color(emissivity_slider, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(emissivity_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(emissivity_slider, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_border_width(emissivity_slider, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(emissivity_slider, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(emissivity_slider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(emissivity_slider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(emissivity_slider, 1, LV_PART_MAIN);

    // Filled track (INDICATOR)
    lv_obj_set_style_bg_color(emissivity_slider, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(emissivity_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(emissivity_slider, 0, LV_PART_INDICATOR);

    // Knob (KNOB)
    lv_obj_set_style_bg_color(emissivity_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(emissivity_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(emissivity_slider, UI_COLOR_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_border_width(emissivity_slider, 2, LV_PART_KNOB);
    lv_obj_set_style_radius(emissivity_slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_ver(emissivity_slider, 4, LV_PART_KNOB);
    lv_obj_set_style_pad_hor(emissivity_slider, 2, LV_PART_KNOB);

    // Edited state for knob & indicator
    lv_obj_set_style_bg_color(emissivity_slider, UI_COLOR_YELLOW, LV_PART_KNOB | LV_STATE_EDITED);
    lv_obj_set_style_border_color(emissivity_slider, UI_COLOR_YELLOW, LV_PART_KNOB | LV_STATE_EDITED);
    lv_obj_set_style_bg_color(emissivity_slider, UI_COLOR_YELLOW, LV_PART_INDICATOR | LV_STATE_EDITED);

    lv_obj_add_event_cb(emissivity_slider, emissivitySliderEventCb, LV_EVENT_ALL, NULL);

    // Status label below slider inside card
    emissivity_status_label = lv_label_create(emissivity_card);
    lv_label_set_text(emissivity_status_label, "[Click knob to adjust]");
    lv_obj_set_pos(emissivity_status_label, 0, 48);
    lv_obj_set_size(emissivity_status_label, 174, 30);
    lv_obj_set_style_text_align(emissivity_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(emissivity_status_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(emissivity_status_label, UI_COLOR_TEXT_DIM, 0);

    curY += 92 + 8;

    // Section Header: PRESETS
    lv_obj_t* secHdr = lv_label_create(emissivity_list);
    lv_label_set_text(secHdr, "COMMON PRESETS");
    lv_obj_set_pos(secHdr, 4, curY);
    lv_obj_set_style_text_color(secHdr, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(secHdr, &lv_font_montserrat_10, 0);
    curY += 16;

    // Create Preset Buttons
    for (int i = 0; i < PRESET_COUNT; ++i) {
        presetBtns[i] = lv_btn_create(emissivity_list);
        lv_obj_set_pos(presetBtns[i], 2, curY);
        lv_obj_set_size(presetBtns[i], 198, itemH);
        lv_obj_add_style(presetBtns[i], &style_btn_default, 0);
        lv_obj_add_style(presetBtns[i], &style_btn_focused, LV_STATE_FOCUSED);
        lv_obj_add_style(presetBtns[i], &style_btn_focused, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(presetBtns[i], &style_btn_pressed, LV_STATE_PRESSED);

        // Arrow indicator
        presetArrows[i] = lv_label_create(presetBtns[i]);
        lv_label_set_text(presetArrows[i], LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(presetArrows[i], UI_COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(presetArrows[i], &lv_font_montserrat_12, 0);
        lv_obj_align(presetArrows[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_flag(presetArrows[i], LV_OBJ_FLAG_HIDDEN);

        // Name
        presetNameLabels[i] = lv_label_create(presetBtns[i]);
        lv_label_set_text(presetNameLabels[i], PRESETS[i].name);
        lv_obj_align(presetNameLabels[i], LV_ALIGN_LEFT_MID, 13, 0);
        lv_obj_set_style_text_font(presetNameLabels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(presetNameLabels[i], UI_COLOR_TEXT, 0);

        // Value readout
        char valBuf[16];
        snprintf(valBuf, sizeof(valBuf), "%.2f", PRESETS[i].value);
        presetValLabels[i] = lv_label_create(presetBtns[i]);
        lv_label_set_text(presetValLabels[i], valBuf);
        lv_obj_align(presetValLabels[i], LV_ALIGN_RIGHT_MID, -3, 0);
        lv_obj_set_style_text_color(presetValLabels[i], UI_COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(presetValLabels[i], &lv_font_montserrat_12, 0);

        lv_obj_add_event_cb(presetBtns[i], presetBtnEventCb, LV_EVENT_ALL, (void*)(intptr_t)i);
        curY += itemH + spacing;
    }

    // Divider
    curY += 4;
    static lv_point_t sep_pts[] = {{4, 0}, {196, 0}};
    lv_obj_t* sep = lv_line_create(emissivity_list);
    lv_line_set_points(sep, sep_pts, 2);
    lv_obj_add_style(sep, &style_divider, 0);
    lv_obj_set_pos(sep, 0, curY);
    curY += 8;

    // Back Button
    emissivity_back_btn = lv_btn_create(emissivity_list);
    lv_obj_set_pos(emissivity_back_btn, 2, curY);
    lv_obj_set_size(emissivity_back_btn, 198, itemH);
    lv_obj_add_style(emissivity_back_btn, &style_btn_default, 0);
    lv_obj_add_style(emissivity_back_btn, &style_btn_back_focused, LV_STATE_FOCUSED);
    lv_obj_add_style(emissivity_back_btn, &style_btn_back_focused, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(emissivity_back_btn, &style_btn_pressed, LV_STATE_PRESSED);

    back_arrow = lv_label_create(emissivity_back_btn);
    lv_label_set_text(back_arrow, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_arrow, lv_color_black(), 0);
    lv_obj_set_style_text_font(back_arrow, &lv_font_montserrat_12, 0);
    lv_obj_align(back_arrow, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(back_arrow, LV_OBJ_FLAG_HIDDEN);

    emissivity_back_label = lv_label_create(emissivity_back_btn);
    lv_label_set_text(emissivity_back_label, "Back to Settings");
    lv_obj_align(emissivity_back_label, LV_ALIGN_LEFT_MID, 13, 0);
    lv_obj_set_style_text_font(emissivity_back_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(emissivity_back_label, UI_COLOR_TEXT_DIM, 0);

    lv_obj_add_event_cb(emissivity_back_btn, emissivityBackBtnEventCb, LV_EVENT_ALL, NULL);

    // Right-Side Contextual Description Panel
    lv_obj_t* hint_hdr = lv_label_create(screen_emissivity);
    lv_label_set_text(hint_hdr, "INFO");
    lv_obj_set_pos(hint_hdr, 222, 38);
    lv_obj_set_style_text_color(hint_hdr, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint_hdr, &lv_font_montserrat_10, 0);

    emissivity_desc_label = lv_label_create(screen_emissivity);
    lv_obj_set_pos(emissivity_desc_label, 222, 54);
    lv_obj_set_size(emissivity_desc_label, 92, 150);
    lv_label_set_long_mode(emissivity_desc_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(emissivity_desc_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(emissivity_desc_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(emissivity_desc_label, "Fine-tune surface emissivity from 0.10 to 1.00. Click to adjust.");

    // Footer Divider Line
    static lv_point_t f_line_pts[] = {{0, 216}, {DISPLAY_WIDTH - 1, 216}};
    lv_obj_t* line_bot = lv_line_create(screen_emissivity);
    lv_line_set_points(line_bot, f_line_pts, 2);
    lv_obj_add_style(line_bot, &style_divider, 0);

    // Footer Navigation Hints
    emissivity_footer_label = lv_label_create(screen_emissivity);
    lv_label_set_text(emissivity_footer_label, "[Rotate] Navigate   |   [Click] Select / Adjust");
    lv_obj_add_style(emissivity_footer_label, &style_footer_text, 0);
    lv_obj_align(emissivity_footer_label, LV_ALIGN_BOTTOM_LEFT, 14, -8);
}
