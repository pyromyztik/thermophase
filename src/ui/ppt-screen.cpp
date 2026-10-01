#include "ui.hpp"
#include "display.hpp"
#include "ppt.hpp"

// Dedicated PPT Screen Object
lv_obj_t* screen_ppt = nullptr;

// Center Thermal / Phase Image Widget & Reticle (180x192 between letterbox bars)
static lv_obj_t* img_ppt_feed = nullptr;
static lv_img_dsc_t ppt_img_dsc;
static lv_obj_t* ppt_reticle = nullptr;

// Top Letterbox Bar (Not overlaid on image, 180x24 at y=0)
static lv_obj_t* ppt_top_bar = nullptr;
static lv_obj_t* ppt_badge_label = nullptr;

// Center Acquisition Progress Box
static lv_obj_t* ppt_progress_box = nullptr;
static lv_obj_t* ppt_progress_title = nullptr;
static lv_obj_t* ppt_progress_count = nullptr;
static lv_obj_t* ppt_progress_bar = nullptr;

// Bottom Letterbox Bar (Not overlaid on image, 180x24 at y=216)
static lv_obj_t* ppt_bot_bar = nullptr;
static lv_obj_t* ppt_hint_label = nullptr;

// Left Telemetry Panel (70x240 at x=0)
static lv_obj_t* ppt_left_panel = nullptr;
static lv_obj_t* ppt_lbl_header = nullptr;
static lv_obj_t* ppt_lbl_f1_title = nullptr;
static lv_obj_t* ppt_lbl_f1_val = nullptr;
static lv_obj_t* ppt_lbl_f1_unit = nullptr;
static lv_obj_t* ppt_lbl_f2_title = nullptr;
static lv_obj_t* ppt_lbl_f2_val = nullptr;
static lv_obj_t* ppt_lbl_f3_title = nullptr;
static lv_obj_t* ppt_lbl_f3_val = nullptr;

// Right Telemetry Panel (70x240 at x=250)
static lv_obj_t* ppt_right_panel = nullptr;
static lv_obj_t* ppt_lbl_depth_hdr = nullptr;
static lv_obj_t* ppt_lbl_depth_val = nullptr;
static lv_obj_t* ppt_lbl_scaler = nullptr;
static lv_obj_t* ppt_canvas_colorbar = nullptr;
static lv_color_t ppt_colorbar_cbuf[14 * 148];
static lv_obj_t* ppt_lbl_cb_max = nullptr;
static lv_obj_t* ppt_lbl_cb_mid = nullptr;
static lv_obj_t* ppt_lbl_cb_min = nullptr;

// Greek letter Pi glyph definition (UTF-8 \xCF\x80, Unicode 0x03C0)
#define UI_SYMBOL_PI "\xCF\x80"

// 4-bpp Anti-aliased Greek Pi glyph (DejaVu Sans Oblique 10px, range 0x03C0)
// Generated via lv_font_conv, falls back to lv_font_montserrat_10 for other glyphs (+, -, numbers, degree)
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap_pi[] = {
    /* U+03C0 "π" */
    0x2f, 0xfe, 0x12, 0x5b, 0xf4, 0xd1, 0x7, 0x50,
    0x52, 0x0, 0x18, 0x88, 0xc0, 0x6, 0xa0, 0xae,
    0x0, 0x63, 0x7, 0x71, 0x0
};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc_pi[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 96, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 0}
};

static const lv_font_fmt_txt_cmap_t cmaps_pi[] = {
    {
        .range_start = 960,
        .range_length = 1,
        .glyph_id_start = 1,
        .unicode_list = NULL,
        .glyph_id_ofs_list = NULL,
        .list_length = 0,
        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

static lv_font_fmt_txt_glyph_cache_t cache_pi;
static const lv_font_fmt_txt_dsc_t font_dsc_pi = {
    .glyph_bitmap = glyph_bitmap_pi,
    .glyph_dsc = glyph_dsc_pi,
    .cmaps = cmaps_pi,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 1,
    .cache = &cache_pi
};

const lv_font_t font_pi_10 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 11,
    .base_line = 2,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = &font_dsc_pi,
    .fallback = &lv_font_montserrat_10,
    .user_data = NULL
};

void buildPPTScreen() {
    screen_ppt = lv_obj_create(NULL);
    lv_obj_add_style(screen_ppt, &style_screen, 0);
    lv_obj_clear_flag(screen_ppt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_ppt, LV_SCROLLBAR_MODE_OFF);

    // 1. Top Letterbox Bar (Clean non-overlaid bar, 144x24 at y=0)
    ppt_top_bar = lv_obj_create(screen_ppt);
    lv_obj_set_pos(ppt_top_bar, 88, 0);
    lv_obj_set_size(ppt_top_bar, 144, 24);
    lv_obj_set_style_bg_color(ppt_top_bar, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ppt_top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ppt_top_bar, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(ppt_top_bar, 1, 0);
    lv_obj_set_style_border_side(ppt_top_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(ppt_top_bar, 0, 0);
    lv_obj_set_style_pad_all(ppt_top_bar, 0, 0);
    lv_obj_clear_flag(ppt_top_bar, LV_OBJ_FLAG_SCROLLABLE);

    ppt_badge_label = lv_label_create(ppt_top_bar);
    lv_label_set_text(ppt_badge_label, "[ READY ]  Click to Record");
    lv_obj_center(ppt_badge_label);
    lv_obj_set_style_text_color(ppt_badge_label, UI_COLOR_YELLOW, 0);
    lv_obj_set_style_text_font(ppt_badge_label, &lv_font_montserrat_10, 0);

    // 2. Center Image Display (144x192 3:4 aspect ratio cleanly between top and bottom letterbox bars)
    img_ppt_feed = lv_img_create(screen_ppt);
    lv_obj_set_pos(img_ppt_feed, 88, 24);
    lv_obj_set_size(img_ppt_feed, 144, 192);

    ppt_img_dsc.header.always_zero = 0;
    ppt_img_dsc.header.w = 144;
    ppt_img_dsc.header.h = 192;
    ppt_img_dsc.data_size = 144 * 192 * sizeof(lv_color_t);
    ppt_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    ppt_img_dsc.data = (const uint8_t*)getThermalFrameBuffer();
    lv_img_set_src(img_ppt_feed, &ppt_img_dsc);

    // 3. Optical Center Reticle (15x15 hairline centered at (153, 113))
    ppt_reticle = lv_obj_create(screen_ppt);
    lv_obj_set_size(ppt_reticle, 15, 15);
    lv_obj_set_pos(ppt_reticle, 88 + (144 / 2) - 7, 24 + (192 / 2) - 7);
    lv_obj_set_style_bg_opa(ppt_reticle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ppt_reticle, 0, 0);
    lv_obj_set_style_pad_all(ppt_reticle, 0, 0);
    lv_obj_set_style_radius(ppt_reticle, 0, 0);
    lv_obj_clear_flag(ppt_reticle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ppt_reticle, LV_OBJ_FLAG_SCROLLABLE);

    static lv_point_t h_pts[] = {{0, 7}, {14, 7}};
    lv_obj_t* line_h = lv_line_create(ppt_reticle);
    lv_line_set_points(line_h, h_pts, 2);
    lv_obj_set_style_line_color(line_h, UI_COLOR_TEXT, 0);
    lv_obj_set_style_line_width(line_h, 1, 0);

    static lv_point_t v_pts[] = {{7, 0}, {7, 14}};
    lv_obj_t* line_v = lv_line_create(ppt_reticle);
    lv_line_set_points(line_v, v_pts, 2);
    lv_obj_set_style_line_color(line_v, UI_COLOR_TEXT, 0);
    lv_obj_set_style_line_width(line_v, 1, 0);

    lv_obj_t* center_dot = lv_obj_create(ppt_reticle);
    lv_obj_set_size(center_dot, 1, 1);
    lv_obj_set_pos(center_dot, 7, 7);
    lv_obj_set_style_radius(center_dot, 0, 0);
    lv_obj_set_style_bg_color(center_dot, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center_dot, 0, 0);

    // 4. Center Acquisition Progress Box (overlay during burst recording, sharp corners, no overlap)
    ppt_progress_box = lv_obj_create(screen_ppt);
    lv_obj_set_size(ppt_progress_box, 136, 68);
    lv_obj_set_pos(ppt_progress_box, 92, 86);
    lv_obj_set_style_bg_color(ppt_progress_box, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ppt_progress_box, LV_OPA_90, 0);
    lv_obj_set_style_border_color(ppt_progress_box, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(ppt_progress_box, 1, 0);
    lv_obj_set_style_radius(ppt_progress_box, 0, 0);
    lv_obj_set_style_pad_all(ppt_progress_box, 4, 0);
    lv_obj_clear_flag(ppt_progress_box, LV_OBJ_FLAG_SCROLLABLE);

    ppt_progress_title = lv_label_create(ppt_progress_box);
    lv_label_set_text(ppt_progress_title, "CAPTURING");
    lv_obj_align(ppt_progress_title, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_text_color(ppt_progress_title, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(ppt_progress_title, &lv_font_montserrat_10, 0);

    ppt_progress_count = lv_label_create(ppt_progress_box);
    lv_label_set_text(ppt_progress_count, "Frame 0 / 64 (0%)");
    lv_obj_align(ppt_progress_count, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_text_color(ppt_progress_count, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ppt_progress_count, &lv_font_montserrat_12, 0);

    ppt_progress_bar = lv_bar_create(ppt_progress_box);
    lv_obj_set_size(ppt_progress_bar, 116, 6);
    lv_obj_align(ppt_progress_bar, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_bar_set_range(ppt_progress_bar, 0, 64);
    lv_bar_set_value(ppt_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(ppt_progress_bar, 0, 0);
    lv_obj_set_style_radius(ppt_progress_bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ppt_progress_bar, UI_COLOR_FOCUS_GREY, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ppt_progress_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);

    lv_obj_add_flag(ppt_progress_box, LV_OBJ_FLAG_HIDDEN);

    // 5. Bottom Letterbox / Control Hints Bar (Full width across screen, 320x24 at y=216)
    ppt_bot_bar = lv_obj_create(screen_ppt);
    lv_obj_set_pos(ppt_bot_bar, 0, 216);
    lv_obj_set_size(ppt_bot_bar, 320, 24);
    lv_obj_set_style_bg_color(ppt_bot_bar, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ppt_bot_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ppt_bot_bar, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(ppt_bot_bar, 1, 0);
    lv_obj_set_style_border_side(ppt_bot_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(ppt_bot_bar, 0, 0);
    lv_obj_set_style_pad_all(ppt_bot_bar, 0, 0);
    lv_obj_clear_flag(ppt_bot_bar, LV_OBJ_FLAG_SCROLLABLE);

    ppt_hint_label = lv_label_create(ppt_bot_bar);
    lv_label_set_text(ppt_hint_label, "[Click] Record | [Hold] Menu");
    lv_obj_center(ppt_hint_label);
    lv_obj_set_style_text_color(ppt_hint_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ppt_hint_label, &lv_font_montserrat_10, 0);

    // 6. Side Telemetry Panel: LEFT (88x216 at x=0)
    ppt_left_panel = lv_obj_create(screen_ppt);
    lv_obj_set_pos(ppt_left_panel, 0, 0);
    lv_obj_set_size(ppt_left_panel, 88, 216);
    lv_obj_set_style_bg_color(ppt_left_panel, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ppt_left_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ppt_left_panel, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(ppt_left_panel, 1, 0);
    lv_obj_set_style_border_side(ppt_left_panel, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_pad_all(ppt_left_panel, 6, 0);
    lv_obj_set_style_radius(ppt_left_panel, 0, 0);
    lv_obj_clear_flag(ppt_left_panel, LV_OBJ_FLAG_SCROLLABLE);

    ppt_lbl_header = lv_label_create(ppt_left_panel);
    lv_label_set_text(ppt_lbl_header, "PPT READY");
    lv_obj_set_style_text_color(ppt_lbl_header, UI_COLOR_YELLOW, 0);
    lv_obj_set_style_text_font(ppt_lbl_header, &lv_font_montserrat_10, 0);
    lv_obj_align(ppt_lbl_header, LV_ALIGN_TOP_LEFT, 4, 6);

    ppt_lbl_f1_title = lv_label_create(ppt_left_panel);
    lv_label_set_text(ppt_lbl_f1_title, "MODE");
    lv_obj_set_style_text_color(ppt_lbl_f1_title, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ppt_lbl_f1_title, &lv_font_montserrat_10, 0);
    lv_obj_align(ppt_lbl_f1_title, LV_ALIGN_TOP_LEFT, 4, 26);

    ppt_lbl_f1_val = lv_label_create(ppt_left_panel);
    lv_label_set_text(ppt_lbl_f1_val, "PREVIEW");
    lv_obj_set_style_text_color(ppt_lbl_f1_val, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ppt_lbl_f1_val, &lv_font_montserrat_12, 0);
    lv_obj_align(ppt_lbl_f1_val, LV_ALIGN_TOP_LEFT, 4, 40);

    ppt_lbl_f1_unit = lv_label_create(ppt_left_panel);
    lv_label_set_text(ppt_lbl_f1_unit, "Look Around");
    lv_obj_set_style_text_color(ppt_lbl_f1_unit, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ppt_lbl_f1_unit, &lv_font_montserrat_10, 0);
    lv_obj_align(ppt_lbl_f1_unit, LV_ALIGN_TOP_LEFT, 4, 56);

    ppt_lbl_f2_title = lv_label_create(ppt_left_panel);
    lv_label_set_text(ppt_lbl_f2_title, "RISE dT");
    lv_obj_set_style_text_color(ppt_lbl_f2_title, UI_COLOR_CYAN, 0);
    lv_obj_set_style_text_font(ppt_lbl_f2_title, &lv_font_montserrat_10, 0);
    lv_obj_align(ppt_lbl_f2_title, LV_ALIGN_TOP_LEFT, 4, 78);

    ppt_lbl_f2_val = lv_label_create(ppt_left_panel);
    lv_label_set_text(ppt_lbl_f2_val, "+0.0 " UI_SYMBOL_DEGREE "C");
    lv_obj_set_style_text_color(ppt_lbl_f2_val, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ppt_lbl_f2_val, &lv_font_montserrat_12, 0);
    lv_obj_align(ppt_lbl_f2_val, LV_ALIGN_TOP_LEFT, 4, 92);

    ppt_lbl_f3_title = lv_label_create(ppt_left_panel);
    lv_label_set_text(ppt_lbl_f3_title, "ACTION");
    lv_obj_set_style_text_color(ppt_lbl_f3_title, UI_COLOR_ORANGE, 0);
    lv_obj_set_style_text_font(ppt_lbl_f3_title, &lv_font_montserrat_10, 0);
    lv_obj_align(ppt_lbl_f3_title, LV_ALIGN_TOP_LEFT, 4, 122);

    ppt_lbl_f3_val = lv_label_create(ppt_left_panel);
    lv_label_set_text(ppt_lbl_f3_val, "[Click]");
    lv_obj_set_style_text_color(ppt_lbl_f3_val, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ppt_lbl_f3_val, &lv_font_montserrat_10, 0);
    lv_obj_align(ppt_lbl_f3_val, LV_ALIGN_TOP_LEFT, 4, 136);

    // Bottom-left Battery & Bluetooth Indicator:
    // Left-aligned at x=4 (matching side telemetry), snapped near bottom, ordered: Battery then Bluetooth icon
    createBatteryIndicator(ppt_left_panel, true, LV_ALIGN_BOTTOM_LEFT, 4, -6, true);

    // 7. Side Telemetry Panel: RIGHT (88x216 at x=232)
    ppt_right_panel = lv_obj_create(screen_ppt);
    lv_obj_set_pos(ppt_right_panel, 232, 0);
    lv_obj_set_size(ppt_right_panel, 88, 216);
    lv_obj_set_style_bg_color(ppt_right_panel, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ppt_right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ppt_right_panel, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(ppt_right_panel, 1, 0);
    lv_obj_set_style_border_side(ppt_right_panel, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_pad_all(ppt_right_panel, 6, 0);
    lv_obj_set_style_radius(ppt_right_panel, 0, 0);
    lv_obj_clear_flag(ppt_right_panel, LV_OBJ_FLAG_SCROLLABLE);

    ppt_lbl_depth_hdr = lv_label_create(ppt_right_panel);
    lv_label_set_text(ppt_lbl_depth_hdr, "STATUS");
    lv_obj_set_style_text_color(ppt_lbl_depth_hdr, UI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(ppt_lbl_depth_hdr, &lv_font_montserrat_10, 0);
    lv_obj_align(ppt_lbl_depth_hdr, LV_ALIGN_TOP_LEFT, 6, 6);

    ppt_lbl_depth_val = lv_label_create(ppt_right_panel);
    lv_label_set_text(ppt_lbl_depth_val, "READY");
    lv_obj_set_style_text_color(ppt_lbl_depth_val, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ppt_lbl_depth_val, &lv_font_montserrat_10, 0);
    lv_obj_align(ppt_lbl_depth_val, LV_ALIGN_TOP_LEFT, 6, 18);

    // Vertical Color Bar Canvas (14x148)
    ppt_canvas_colorbar = lv_canvas_create(ppt_right_panel);
    lv_canvas_set_buffer(ppt_canvas_colorbar, ppt_colorbar_cbuf, 14, 148, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(ppt_canvas_colorbar, 6, 34);
    lv_obj_set_style_border_color(ppt_canvas_colorbar, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(ppt_canvas_colorbar, 1, 0);
    lv_obj_set_style_radius(ppt_canvas_colorbar, 0, 0);

    ppt_lbl_cb_max = lv_label_create(ppt_right_panel);
    lv_label_set_text(ppt_lbl_cb_max, "--" UI_SYMBOL_DEGREE);
    lv_obj_set_style_text_color(ppt_lbl_cb_max, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ppt_lbl_cb_max, &font_pi_10, 0);
    lv_obj_align(ppt_lbl_cb_max, LV_ALIGN_TOP_LEFT, 26, 34);

    ppt_lbl_cb_mid = lv_label_create(ppt_right_panel);
    lv_label_set_text(ppt_lbl_cb_mid, "--" UI_SYMBOL_DEGREE);
    lv_obj_set_style_text_color(ppt_lbl_cb_mid, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ppt_lbl_cb_mid, &font_pi_10, 0);
    lv_obj_align(ppt_lbl_cb_mid, LV_ALIGN_TOP_LEFT, 26, 102);

    ppt_lbl_cb_min = lv_label_create(ppt_right_panel);
    lv_label_set_text(ppt_lbl_cb_min, "--" UI_SYMBOL_DEGREE);
    lv_obj_set_style_text_color(ppt_lbl_cb_min, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ppt_lbl_cb_min, &font_pi_10, 0);
    lv_obj_align(ppt_lbl_cb_min, LV_ALIGN_TOP_LEFT, 26, 170);

    ppt_lbl_scaler = lv_label_create(ppt_right_panel);
    lv_label_set_text(ppt_lbl_scaler, "BICUB");
    lv_obj_set_style_text_color(ppt_lbl_scaler, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ppt_lbl_scaler, &lv_font_montserrat_10, 0);
    lv_obj_align(ppt_lbl_scaler, LV_ALIGN_TOP_LEFT, 6, 190);
}

void updatePPTScreenArmed(
    float currentDt, 
    float thresh, 
    float minTemp, 
    float maxTemp, 
    TempUnit unit, 
    const uint16_t* activePalette
) {
    if (screen_ppt == nullptr) return;

    // Top letterbox status readout
    lv_label_set_text(ppt_badge_label, "[ READY ]  Click to Record");
    lv_obj_set_style_text_color(ppt_badge_label, UI_COLOR_YELLOW, 0);

    // Hide acquisition progress card
    lv_obj_add_flag(ppt_progress_box, LV_OBJ_FLAG_HIDDEN);

    // Bottom letterbox hint
    lv_label_set_text(ppt_hint_label, "[Click] Record | [Hold] Menu");

    // Left Panel
    lv_label_set_text(ppt_lbl_header, "PULSE");
    lv_obj_set_style_text_color(ppt_lbl_header, UI_COLOR_YELLOW, 0);

    lv_label_set_text(ppt_lbl_f1_title, "MODE");
    lv_label_set_text(ppt_lbl_f1_val, "PREVIEW");

    lv_label_set_text(ppt_lbl_f2_title, "RISE dT");
    char buf[32];
    snprintf(buf, sizeof(buf), "+%.1f %s", currentDt, UI_SYMBOL_DEGREE "C");
    lv_label_set_text(ppt_lbl_f2_val, buf);

    lv_label_set_text(ppt_lbl_f3_title, "ACTION");
    lv_label_set_text(ppt_lbl_f3_val, "[Click]");

    // Right Panel
    lv_label_set_text(ppt_lbl_depth_hdr, "STATUS");
    lv_label_set_text(ppt_lbl_depth_val, "READY");

    // Update colorbar canvas with active thermal palette
    static const uint16_t* lastPreviewPalette = nullptr;
    if (activePalette != nullptr && activePalette != lastPreviewPalette) {
        lastPreviewPalette = activePalette;
        for (int y = 0; y < 148; ++y) {
            int palIdx = 255 - (y * 255) / 147;
            lv_color_t c;
            c.full = activePalette[palIdx];
            for (int x = 0; x < 14; ++x) {
                ppt_colorbar_cbuf[y * 14 + x] = c;
            }
        }
        lv_obj_invalidate(ppt_canvas_colorbar);
    }

    if (maxTemp > minTemp) {
        char tbuf[16];
        if (unit == TEMP_UNIT_KELVIN) {
            snprintf(tbuf, sizeof(tbuf), "%.0f", convertTemp(maxTemp, unit));
        } else {
            snprintf(tbuf, sizeof(tbuf), "%.0f" UI_SYMBOL_DEGREE, convertTemp(maxTemp, unit));
        }
        lv_label_set_text(ppt_lbl_cb_max, tbuf);

        float midTemp = 0.5f * (minTemp + maxTemp);
        if (unit == TEMP_UNIT_KELVIN) {
            snprintf(tbuf, sizeof(tbuf), "%.0f", convertTemp(midTemp, unit));
        } else {
            snprintf(tbuf, sizeof(tbuf), "%.0f" UI_SYMBOL_DEGREE, convertTemp(midTemp, unit));
        }
        lv_label_set_text(ppt_lbl_cb_mid, tbuf);

        if (unit == TEMP_UNIT_KELVIN) {
            snprintf(tbuf, sizeof(tbuf), "%.0f", convertTemp(minTemp, unit));
        } else {
            snprintf(tbuf, sizeof(tbuf), "%.0f" UI_SYMBOL_DEGREE, convertTemp(minTemp, unit));
        }
        lv_label_set_text(ppt_lbl_cb_min, tbuf);
    }

    lv_obj_invalidate(img_ppt_feed);
}

void updatePPTScreenAcquiring(int currentFrame, int totalFrames) {
    if (screen_ppt == nullptr) return;

    // Top letterbox status readout
    char bBuf[64];
    snprintf(bBuf, sizeof(bBuf), "[ RECORDING ]  %d/%d", currentFrame, totalFrames);
    lv_label_set_text(ppt_badge_label, bBuf);
    lv_obj_set_style_text_color(ppt_badge_label, UI_COLOR_ACCENT, 0);

    // Show center acquisition progress card
    lv_obj_clear_flag(ppt_progress_box, LV_OBJ_FLAG_HIDDEN);
    int pct = (currentFrame * 100) / totalFrames;
    char pBuf[48];
    snprintf(pBuf, sizeof(pBuf), "Frame %d / %d (%d%%)", currentFrame, totalFrames, pct);
    lv_label_set_text(ppt_progress_count, pBuf);
    lv_bar_set_value(ppt_progress_bar, currentFrame, LV_ANIM_OFF);

    // Bottom letterbox hint
    lv_label_set_text(ppt_hint_label, "Recording... | [Hold] Menu");

    // Left panel
    lv_label_set_text(ppt_lbl_header, "PPT RECORD");
    lv_obj_set_style_text_color(ppt_lbl_header, UI_COLOR_ACCENT, 0);

    lv_label_set_text(ppt_lbl_f1_title, "BURST");
    lv_label_set_text(ppt_lbl_f1_val, "16 Hz");
    lv_label_set_text(ppt_lbl_f1_unit, "Stream");

    lv_label_set_text(ppt_lbl_f2_title, "PROGRESS");
    snprintf(pBuf, sizeof(pBuf), "%d / %d", currentFrame, totalFrames);
    lv_label_set_text(ppt_lbl_f2_val, pBuf);

    lv_label_set_text(ppt_lbl_f3_title, "STATUS");
    lv_label_set_text(ppt_lbl_f3_val, "LOCK");

    // Right panel
    lv_label_set_text(ppt_lbl_depth_hdr, "STATUS");
    lv_label_set_text(ppt_lbl_depth_val, "CAPTURING");

    lv_obj_invalidate(img_ppt_feed);
}

void updatePPTScreenInspect(
    int binK,
    float freqHz,
    float spotPhase,
    float minPhase,
    float maxPhase,
    const char* depthDesc,
    ScalingMode mode,
    const uint16_t* activePalette
) {
    if (screen_ppt == nullptr) return;

    // Top letterbox status readout
    char bBuf[64];
    snprintf(bBuf, sizeof(bBuf), "[ PPT ]  %.2f Hz (Bin %d/16)", freqHz, binK);
    lv_label_set_text(ppt_badge_label, bBuf);
    lv_obj_set_style_text_color(ppt_badge_label, UI_COLOR_CYAN, 0);

    // Hide acquisition progress card
    lv_obj_add_flag(ppt_progress_box, LV_OBJ_FLAG_HIDDEN);

    // Bottom letterbox hint
    lv_label_set_text(ppt_hint_label, "[Rot] Freq | [Click] Back | [Hold] Menu");

    // Left panel: Pure phase telemetry
    lv_label_set_text(ppt_lbl_header, "PPT PHASE");
    lv_obj_set_style_text_color(ppt_lbl_header, UI_COLOR_YELLOW, 0);

    lv_label_set_text(ppt_lbl_f1_title, "FREQ");
    char fBuf[32];
    snprintf(fBuf, sizeof(fBuf), "%.2f Hz", freqHz);
    lv_label_set_text(ppt_lbl_f1_val, fBuf);

    char binBuf[16];
    snprintf(binBuf, sizeof(binBuf), "Bin %d/16", binK);
    lv_label_set_text(ppt_lbl_f1_unit, binBuf);

    lv_label_set_text(ppt_lbl_f2_title, "CENTER PH");
    char phBuf[32];
    snprintf(phBuf, sizeof(phBuf), "%+.2f rad", spotPhase);
    lv_label_set_text(ppt_lbl_f2_val, phBuf);

    lv_label_set_text(ppt_lbl_f3_title, "PH SPAN");
    char spanBuf[32];
    snprintf(spanBuf, sizeof(spanBuf), "[%+.1f,%+.1f]", minPhase, maxPhase);
    lv_label_set_text(ppt_lbl_f3_val, spanBuf);

    // Right panel: Depth classification & Color bar
    lv_label_set_text(ppt_lbl_depth_hdr, "DEPTH");
    lv_label_set_text(ppt_lbl_depth_val, depthDesc);

    const char* modeNames[5] = {"NEAR", "BILIN", "BICUB", "LANCZ", "TAAU"};
    lv_label_set_text(ppt_lbl_scaler, modeNames[mode % 5]);

    // Update colorbar canvas if palette changed
    static const uint16_t* lastRenderedPalette = nullptr;
    if (activePalette != nullptr && activePalette != lastRenderedPalette) {
        lastRenderedPalette = activePalette;
        for (int y = 0; y < 148; ++y) {
            int palIdx = 255 - (y * 255) / 147;
            lv_color_t c;
            c.full = activePalette[palIdx];
            for (int x = 0; x < 14; ++x) {
                ppt_colorbar_cbuf[y * 14 + x] = c;
            }
        }
        lv_obj_invalidate(ppt_canvas_colorbar);
    }

    lv_label_set_text(ppt_lbl_cb_max, "+" UI_SYMBOL_PI);
    lv_label_set_text(ppt_lbl_cb_mid, " 0");
    lv_label_set_text(ppt_lbl_cb_min, "-" UI_SYMBOL_PI);

    lv_obj_invalidate(img_ppt_feed);
}


