#include "ui.hpp"
#include "display.hpp"

// Live Thermal View Widgets
static lv_obj_t* img_thermal_feed = nullptr;
static lv_img_dsc_t thermal_img_dsc;
static lv_obj_t* thermal_reticle_container = nullptr;

// Side HUD Widgets (for 3:4 aspect ratio)
static lv_obj_t* hud_left_panel = nullptr;
static lv_obj_t* hud_right_panel = nullptr;
static lv_obj_t* title_thm = nullptr;
static lv_obj_t* lbl_cnt = nullptr;
static lv_obj_t* label_center_temp = nullptr;
static lv_obj_t* label_temp_unit = nullptr;
static lv_obj_t* lbl_min_hdr = nullptr;
static lv_obj_t* label_min_temp = nullptr;
static lv_obj_t* lbl_max_hdr = nullptr;
static lv_obj_t* label_max_temp = nullptr;
static lv_obj_t* label_hint_left = nullptr;
static lv_obj_t* label_hint_right = nullptr;
static lv_obj_t* label_fps = nullptr;
static lv_obj_t* label_scaling_mode = nullptr;
static lv_obj_t* canvas_colorbar = nullptr;
static lv_color_t colorbar_cbuf[16 * 160];
static lv_obj_t* label_colorbar_max = nullptr;
static lv_obj_t* label_colorbar_mid = nullptr;
static lv_obj_t* label_colorbar_min = nullptr;

// Fullscreen Control Hint
static lv_obj_t* fs_escape_hint_badge = nullptr;
static lv_obj_t* fs_escape_hint_label = nullptr;
static bool thermalIsFullscreen = false;

// Snapshot Capture Flash Rectangle
static lv_obj_t* snapshot_flash_rect = nullptr;

// Thermal Range Progress Bar Overlay Widgets
static lv_obj_t* range_pb_box = nullptr;
static lv_obj_t* range_pb_bar = nullptr;
static lv_obj_t* range_pb_label = nullptr;

void uiTriggerSnapshotFlash() {
    if (snapshot_flash_rect == nullptr) return;

    // Reposition flash rectangle according to current mode (fullscreen vs windowed)
    if (thermalIsFullscreen) {
        lv_obj_set_pos(snapshot_flash_rect, 0, 0);
        lv_obj_set_size(snapshot_flash_rect, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    } else {
        lv_obj_set_pos(snapshot_flash_rect, 70, 0);
        lv_obj_set_size(snapshot_flash_rect, 180, 240);
    }

    // Cancel any ongoing flash animation and start fresh
    lv_anim_del(snapshot_flash_rect, NULL);
    lv_obj_set_style_border_opa(snapshot_flash_rect, LV_OPA_COVER, 0);
    lv_obj_clear_flag(snapshot_flash_rect, LV_OBJ_FLAG_HIDDEN);

    // Fade border opacity from 255 (LV_OPA_COVER) down to 0 over 1500ms (1.5 seconds)
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, snapshot_flash_rect);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, 1500);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void* var, int32_t val) {
        lv_obj_set_style_border_opa((lv_obj_t*)var, (lv_opa_t)val, 0);
    });
    lv_anim_set_ready_cb(&a, [](lv_anim_t* anim) {
        lv_obj_add_flag((lv_obj_t*)anim->var, LV_OBJ_FLAG_HIDDEN);
    });
    lv_anim_start(&a);

    Serial.println("[UI] Snapshot capture flash triggered (1.5s fade)");
}

bool uiIsFullscreen() {
    return thermalIsFullscreen;
}

void uiEnterFullscreen() {
    if (thermalIsFullscreen) return;
    thermalIsFullscreen = true;

    if (hud_left_panel) lv_obj_add_flag(hud_left_panel, LV_OBJ_FLAG_HIDDEN);
    if (hud_right_panel) lv_obj_add_flag(hud_right_panel, LV_OBJ_FLAG_HIDDEN);

    if (img_thermal_feed) {
        lv_obj_set_pos(img_thermal_feed, 0, 0);
        lv_obj_set_size(img_thermal_feed, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        thermal_img_dsc.header.w = DISPLAY_WIDTH;
        thermal_img_dsc.header.h = DISPLAY_HEIGHT;
        thermal_img_dsc.data_size = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t);
        lv_img_set_src(img_thermal_feed, &thermal_img_dsc);
    }

    if (thermal_reticle_container) {
        lv_obj_set_pos(thermal_reticle_container, (DISPLAY_WIDTH / 2) - 7, (DISPLAY_HEIGHT / 2) - 7);
    }

    if (fs_escape_hint_badge) {
        lv_anim_del(fs_escape_hint_badge, NULL);
        lv_obj_set_pos(fs_escape_hint_badge, (DISPLAY_WIDTH - 180) / 2, 208);
        lv_obj_clear_flag(fs_escape_hint_badge, LV_OBJ_FLAG_HIDDEN);

        // Slide down offscreen after 4 seconds (4000ms delay, 350ms duration)
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, fs_escape_hint_badge);
        lv_anim_set_values(&a, 208, 245);
        lv_anim_set_time(&a, 350);
        lv_anim_set_delay(&a, 4000);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_set_exec_cb(&a, [](void* var, int32_t val) {
            lv_obj_set_y((lv_obj_t*)var, val);
        });
        lv_anim_set_ready_cb(&a, [](lv_anim_t* anim) {
            lv_obj_add_flag((lv_obj_t*)anim->var, LV_OBJ_FLAG_HIDDEN);
        });
        lv_anim_start(&a);
    }

    Serial.println("[UI] Entered Fullscreen Thermal View (320x240, Rotated 90 deg)");
}

void uiExitFullscreen() {
    if (!thermalIsFullscreen) return;
    thermalIsFullscreen = false;

    if (fs_escape_hint_badge) {
        lv_anim_del(fs_escape_hint_badge, NULL);
        lv_obj_add_flag(fs_escape_hint_badge, LV_OBJ_FLAG_HIDDEN);
    }

    if (img_thermal_feed) {
        lv_obj_set_pos(img_thermal_feed, 70, 0);
        lv_obj_set_size(img_thermal_feed, 180, 240);
        thermal_img_dsc.header.w = 180;
        thermal_img_dsc.header.h = 240;
        thermal_img_dsc.data_size = 180 * 240 * sizeof(lv_color_t);
        lv_img_set_src(img_thermal_feed, &thermal_img_dsc);
    }

    if (hud_left_panel) lv_obj_clear_flag(hud_left_panel, LV_OBJ_FLAG_HIDDEN);
    if (hud_right_panel) lv_obj_clear_flag(hud_right_panel, LV_OBJ_FLAG_HIDDEN);

    if (thermal_reticle_container) {
        lv_obj_set_pos(thermal_reticle_container, (DISPLAY_WIDTH / 2) - 7, (DISPLAY_HEIGHT / 2) - 7);
    }

    Serial.println("[UI] Exited Fullscreen Thermal View -> Restored 180x240 Portrait View");
}

void uiToggleFullscreen() {
    if (thermalIsFullscreen) {
        uiExitFullscreen();
    } else {
        uiEnterFullscreen();
    }
}

lv_obj_t* getThermalImageObj() {
    return img_thermal_feed;
}

void buildThermalScreen() {
    screen_thermal = lv_obj_create(NULL);
    lv_obj_add_style(screen_thermal, &style_screen, 0);
    lv_obj_clear_flag(screen_thermal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_thermal, LV_SCROLLBAR_MODE_OFF);

    // 1. Thermal image widget
    img_thermal_feed = lv_img_create(screen_thermal);
    lv_obj_set_pos(img_thermal_feed, 70, 0);
    lv_obj_set_size(img_thermal_feed, 180, 240);

    // Configure image descriptor pointing to external PSRAM buffer
    thermal_img_dsc.header.always_zero = 0;
    thermal_img_dsc.header.w = 180;
    thermal_img_dsc.header.h = 240;
    thermal_img_dsc.data_size = 180 * 240 * sizeof(lv_color_t);
    thermal_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    thermal_img_dsc.data = (const uint8_t*)getThermalFrameBuffer();
    lv_img_set_src(img_thermal_feed, &thermal_img_dsc);

    // 2. Reticle / Center Crosshair (15x15 px exact match of original design)
    thermal_reticle_container = lv_obj_create(screen_thermal);
    lv_obj_set_size(thermal_reticle_container, 15, 15);
    lv_obj_set_pos(thermal_reticle_container, (DISPLAY_WIDTH / 2) - 7, (DISPLAY_HEIGHT / 2) - 7);
    lv_obj_set_style_bg_opa(thermal_reticle_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(thermal_reticle_container, 0, 0);
    lv_obj_set_style_pad_all(thermal_reticle_container, 0, 0);
    lv_obj_clear_flag(thermal_reticle_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(thermal_reticle_container, LV_OBJ_FLAG_SCROLLABLE);

    // Reticle Horizontal Hairline (15 px white line at y = 7)
    static lv_point_t h_pts[] = {{0, 7}, {14, 7}};
    lv_obj_t* line_h = lv_line_create(thermal_reticle_container);
    lv_line_set_points(line_h, h_pts, 2);
    lv_obj_set_style_line_color(line_h, UI_COLOR_TEXT, 0);
    lv_obj_set_style_line_width(line_h, 1, 0);

    // Reticle Vertical Hairline (15 px white line at x = 7)
    static lv_point_t v_pts[] = {{7, 0}, {7, 14}};
    lv_obj_t* line_v = lv_line_create(thermal_reticle_container);
    lv_line_set_points(line_v, v_pts, 2);
    lv_obj_set_style_line_color(line_v, UI_COLOR_TEXT, 0);
    lv_obj_set_style_line_width(line_v, 1, 0);

    // Center Red Target Pixel at (7, 7)
    lv_obj_t* center_dot = lv_obj_create(thermal_reticle_container);
    lv_obj_set_size(center_dot, 1, 1);
    lv_obj_set_pos(center_dot, 7, 7);
    lv_obj_set_style_radius(center_dot, 0, 0);
    lv_obj_set_style_bg_color(center_dot, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center_dot, 0, 0);
    lv_obj_set_style_pad_all(center_dot, 0, 0);

    // 3. Side HUD Panel: LEFT (Telemetry)
    hud_left_panel = lv_obj_create(screen_thermal);
    lv_obj_set_pos(hud_left_panel, 0, 0);
    lv_obj_set_size(hud_left_panel, 70, 240);
    lv_obj_set_style_bg_color(hud_left_panel, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(hud_left_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hud_left_panel, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(hud_left_panel, 1, 0);
    lv_obj_set_style_border_side(hud_left_panel, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(hud_left_panel, 0, 0);
    lv_obj_set_style_pad_all(hud_left_panel, 4, 0);
    lv_obj_clear_flag(hud_left_panel, LV_OBJ_FLAG_SCROLLABLE);

    title_thm = lv_label_create(hud_left_panel);
    lv_label_set_text(title_thm, "THERMAL");
    lv_obj_set_style_text_color(title_thm, UI_COLOR_YELLOW, 0);
    lv_obj_set_style_text_font(title_thm, &lv_font_montserrat_10, 0);
    lv_obj_align(title_thm, LV_ALIGN_TOP_LEFT, 2, 4);

    lbl_cnt = lv_label_create(hud_left_panel);
    lv_label_set_text(lbl_cnt, "CENTER");
    lv_obj_set_style_text_color(lbl_cnt, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_cnt, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_cnt, LV_ALIGN_TOP_LEFT, 2, 26);

    label_center_temp = lv_label_create(hud_left_panel);
    lv_label_set_text(label_center_temp, "--.-");
    lv_obj_set_style_text_color(label_center_temp, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_center_temp, &lv_font_montserrat_20, 0);
    lv_obj_align(label_center_temp, LV_ALIGN_TOP_LEFT, 2, 42);

    label_temp_unit = lv_label_create(hud_left_panel);
    lv_label_set_text(label_temp_unit, "Celsius");
    lv_obj_set_style_text_color(label_temp_unit, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_temp_unit, &lv_font_montserrat_10, 0);
    lv_obj_align(label_temp_unit, LV_ALIGN_TOP_LEFT, 2, 68);

    lbl_min_hdr = lv_label_create(hud_left_panel);
    lv_label_set_text(lbl_min_hdr, "MIN TEMP");
    lv_obj_set_style_text_color(lbl_min_hdr, UI_COLOR_CYAN, 0);
    lv_obj_set_style_text_font(lbl_min_hdr, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_min_hdr, LV_ALIGN_TOP_LEFT, 2, 102);

    label_min_temp = lv_label_create(hud_left_panel);
    lv_label_set_text(label_min_temp, "--.- " UI_SYMBOL_DEGREE "C");
    lv_obj_set_style_text_color(label_min_temp, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_min_temp, &lv_font_montserrat_12, 0);
    lv_obj_align(label_min_temp, LV_ALIGN_TOP_LEFT, 2, 118);

    lbl_max_hdr = lv_label_create(hud_left_panel);
    lv_label_set_text(lbl_max_hdr, "MAX TEMP");
    lv_obj_set_style_text_color(lbl_max_hdr, UI_COLOR_ORANGE, 0);
    lv_obj_set_style_text_font(lbl_max_hdr, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_max_hdr, LV_ALIGN_TOP_LEFT, 2, 142);

    label_max_temp = lv_label_create(hud_left_panel);
    lv_label_set_text(label_max_temp, "--.- " UI_SYMBOL_DEGREE "C");
    lv_obj_set_style_text_color(label_max_temp, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_max_temp, &lv_font_montserrat_12, 0);
    lv_obj_align(label_max_temp, LV_ALIGN_TOP_LEFT, 2, 158);

    label_fps = lv_label_create(hud_left_panel);
    lv_label_set_text(label_fps, "0.0 FPS");
    lv_obj_set_style_text_color(label_fps, UI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(label_fps, &lv_font_montserrat_10, 0);
    lv_obj_align(label_fps, LV_ALIGN_BOTTOM_LEFT, 2, -22);

    // Left Control hint in bottom-left black bar
    label_hint_left = lv_label_create(hud_left_panel);
    lv_label_set_text(label_hint_left, "[Click] Snap");
    lv_obj_set_style_text_color(label_hint_left, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_hint_left, &lv_font_montserrat_8, 0);
    lv_obj_align(label_hint_left, LV_ALIGN_BOTTOM_LEFT, 2, -6);

    // 4. Side HUD Panel: RIGHT (Battery, Scaler Mode & Color Bar)
    hud_right_panel = lv_obj_create(screen_thermal);
    lv_obj_set_pos(hud_right_panel, 250, 0);
    lv_obj_set_size(hud_right_panel, 70, 240);
    lv_obj_set_style_bg_color(hud_right_panel, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(hud_right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hud_right_panel, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(hud_right_panel, 1, 0);
    lv_obj_set_style_border_side(hud_right_panel, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(hud_right_panel, 0, 0);
    lv_obj_set_style_pad_all(hud_right_panel, 4, 0);
    lv_obj_clear_flag(hud_right_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Top-right Battery Indicator (Aligned vertically with "THERMAL" header on left panel)
    createBatteryIndicator(hud_right_panel, true, LV_ALIGN_TOP_RIGHT, -2, 1);

    label_scaling_mode = lv_label_create(hud_right_panel);
    lv_label_set_text(label_scaling_mode, "BICUB");
    lv_obj_set_style_text_color(label_scaling_mode, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_scaling_mode, &lv_font_montserrat_10, 0);
    lv_obj_align(label_scaling_mode, LV_ALIGN_TOP_LEFT, 4, 18);

    // Vertical Color Bar Canvas (16x160)
    canvas_colorbar = lv_canvas_create(hud_right_panel);
    lv_canvas_set_buffer(canvas_colorbar, colorbar_cbuf, 16, 160, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(canvas_colorbar, 6, 36);
    lv_obj_set_style_border_color(canvas_colorbar, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(canvas_colorbar, 1, 0);
    lv_obj_set_style_radius(canvas_colorbar, 0, 0);

    label_colorbar_max = lv_label_create(hud_right_panel);
    lv_label_set_text(label_colorbar_max, "35" UI_SYMBOL_DEGREE);
    lv_obj_set_style_text_color(label_colorbar_max, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_colorbar_max, &lv_font_montserrat_10, 0);
    lv_obj_align(label_colorbar_max, LV_ALIGN_TOP_LEFT, 26, 36);

    label_colorbar_mid = lv_label_create(hud_right_panel);
    lv_label_set_text(label_colorbar_mid, "27" UI_SYMBOL_DEGREE);
    lv_obj_set_style_text_color(label_colorbar_mid, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_colorbar_mid, &lv_font_montserrat_10, 0);
    lv_obj_align(label_colorbar_mid, LV_ALIGN_TOP_LEFT, 26, 110);

    label_colorbar_min = lv_label_create(hud_right_panel);
    lv_label_set_text(label_colorbar_min, "20" UI_SYMBOL_DEGREE);
    lv_obj_set_style_text_color(label_colorbar_min, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_colorbar_min, &lv_font_montserrat_10, 0);
    lv_obj_align(label_colorbar_min, LV_ALIGN_TOP_LEFT, 26, 184);

    // Right Control hint in bottom-right black bar
    label_hint_right = lv_label_create(hud_right_panel);
    lv_label_set_text(label_hint_right, "[Hold] Menu");
    lv_obj_set_style_text_color(label_hint_right, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_hint_right, &lv_font_montserrat_8, 0);
    lv_obj_align(label_hint_right, LV_ALIGN_BOTTOM_LEFT, 4, -6);

    // 5. Fullscreen Escape Hint Badge (Centred at bottom, hidden initially)
    fs_escape_hint_badge = lv_obj_create(screen_thermal);
    lv_obj_set_size(fs_escape_hint_badge, 180, 24);
    lv_obj_set_pos(fs_escape_hint_badge, (DISPLAY_WIDTH - 180) / 2, 208);
    lv_obj_set_style_bg_color(fs_escape_hint_badge, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(fs_escape_hint_badge, LV_OPA_80, 0);
    lv_obj_set_style_border_color(fs_escape_hint_badge, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(fs_escape_hint_badge, 1, 0);
    lv_obj_set_style_radius(fs_escape_hint_badge, 4, 0);
    lv_obj_set_style_pad_all(fs_escape_hint_badge, 0, 0);
    lv_obj_clear_flag(fs_escape_hint_badge, LV_OBJ_FLAG_SCROLLABLE);

    fs_escape_hint_label = lv_label_create(fs_escape_hint_badge);
    lv_label_set_text(fs_escape_hint_label, "[Dbl-Click] Exit Fullscreen");
    lv_obj_center(fs_escape_hint_label);
    lv_obj_set_style_text_color(fs_escape_hint_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(fs_escape_hint_label, &lv_font_montserrat_10, 0);

    lv_obj_add_flag(fs_escape_hint_badge, LV_OBJ_FLAG_HIDDEN);

    // 6. Snapshot Capture White Flash Rectangle (Border around thermal view)
    snapshot_flash_rect = lv_obj_create(screen_thermal);
    lv_obj_set_pos(snapshot_flash_rect, 70, 0);
    lv_obj_set_size(snapshot_flash_rect, 180, 240);
    lv_obj_set_style_bg_opa(snapshot_flash_rect, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(snapshot_flash_rect, lv_color_white(), 0);
    lv_obj_set_style_border_width(snapshot_flash_rect, 4, 0);
    lv_obj_set_style_border_opa(snapshot_flash_rect, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(snapshot_flash_rect, 0, 0);
    lv_obj_set_style_pad_all(snapshot_flash_rect, 0, 0);
    lv_obj_clear_flag(snapshot_flash_rect, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(snapshot_flash_rect, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(snapshot_flash_rect, LV_OBJ_FLAG_HIDDEN);

    // 7. Thermal Range Gesture Progress Bar Overlay (grows symmetrically from center)
    // Box dimensions: 140x18 px, 1px white border (matching UI buttons), 75% black background
    range_pb_box = lv_obj_create(screen_thermal);
    lv_obj_set_size(range_pb_box, 140, 18);
    lv_obj_set_pos(range_pb_box, (DISPLAY_WIDTH - 140) / 2, 195);
    lv_obj_set_style_bg_color(range_pb_box, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(range_pb_box, 191, 0); // 75% opacity
    lv_obj_set_style_border_color(range_pb_box, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(range_pb_box, 1, 0); // 1px white border matching UI buttons
    lv_obj_set_style_radius(range_pb_box, 0, 0); // Square edges matching UI buttons
    lv_obj_set_style_pad_all(range_pb_box, 0, 0);
    lv_obj_clear_flag(range_pb_box, LV_OBJ_FLAG_SCROLLABLE);

    // Inner Red Bar that grows from center outwards
    range_pb_bar = lv_obj_create(range_pb_box);
    lv_obj_set_pos(range_pb_bar, 69, 0);
    lv_obj_set_size(range_pb_bar, 0, 16);
    lv_obj_set_style_bg_color(range_pb_bar, UI_COLOR_ACCENT, 0); // Red
    lv_obj_set_style_bg_opa(range_pb_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(range_pb_bar, 0, 0);
    lv_obj_set_style_radius(range_pb_bar, 0, 0);
    lv_obj_set_style_pad_all(range_pb_bar, 0, 0);
    lv_obj_clear_flag(range_pb_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Text label on progress bar
    range_pb_label = lv_label_create(range_pb_box);
    lv_label_set_text(range_pb_label, "THERMAL RANGE");
    lv_obj_set_style_text_font(range_pb_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(range_pb_label, UI_COLOR_TEXT, 0);
    lv_obj_center(range_pb_label);

    lv_obj_set_style_opa(range_pb_box, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(range_pb_box, LV_OBJ_FLAG_HIDDEN);
}

static bool s_pb_visible = false;
static bool s_pb_fading_out = false;

static void pb_anim_opa_cb(void* var, int32_t val) {
    lv_obj_set_style_opa((lv_obj_t*)var, (lv_opa_t)val, 0);
}

void showThermalRangeProgressBar(int currentTicks, int maxTicks) {
    if (range_pb_box == nullptr || range_pb_bar == nullptr) return;
    if (maxTicks <= 0) maxTicks = 10;
    if (currentTicks < 0) currentTicks = 0;
    if (currentTicks > maxTicks) currentTicks = maxTicks;

    // Available inner width = 138 px (140 - 2*1px border)
    constexpr int totalW = 138;
    int fillW = (totalW * currentTicks) / maxTicks;
    int posX = (totalW - fillW) / 2;

    lv_obj_set_pos(range_pb_bar, posX, 0);
    lv_obj_set_size(range_pb_bar, fillW, 16);

    // If hidden or currently fading out, trigger quick fade in animation
    if (!s_pb_visible || s_pb_fading_out) {
        lv_anim_del(range_pb_box, NULL);
        s_pb_fading_out = false;
        s_pb_visible = true;
        lv_obj_clear_flag(range_pb_box, LV_OBJ_FLAG_HIDDEN);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, range_pb_box);
        lv_anim_set_values(&a, lv_obj_get_style_opa(range_pb_box, 0), LV_OPA_COVER);
        lv_anim_set_time(&a, 150); // Quick fade-in (150ms)
        lv_anim_set_exec_cb(&a, pb_anim_opa_cb);
        lv_anim_start(&a);
    }
}

void hideThermalRangeProgressBar(bool animate) {
    if (range_pb_box == nullptr) return;

    if (!animate) {
        lv_anim_del(range_pb_box, NULL);
        s_pb_visible = false;
        s_pb_fading_out = false;
        lv_obj_set_style_opa(range_pb_box, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(range_pb_box, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (!s_pb_visible || s_pb_fading_out) return;

    s_pb_fading_out = true;
    lv_anim_del(range_pb_box, NULL);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, range_pb_box);
    lv_anim_set_values(&a, lv_obj_get_style_opa(range_pb_box, 0), LV_OPA_TRANSP);
    lv_anim_set_time(&a, 250); // Smooth fade-out (250ms)
    lv_anim_set_exec_cb(&a, pb_anim_opa_cb);
    lv_anim_set_ready_cb(&a, [](lv_anim_t* anim) {
        lv_obj_t* obj = (lv_obj_t*)anim->var;
        if (obj) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
        s_pb_visible = false;
        s_pb_fading_out = false;
    });
    lv_anim_start(&a);
}

void updateThermalHUD(
    float centerTemp,
    float sensorMinTemp,
    float sensorMaxTemp,
    float colorMinTemp,
    float colorMaxTemp,
    float fps,
    ScalingMode mode,
    TempUnit unit,
    const uint16_t* activePalette,
    int vpX,
    int vpY,
    int vpW,
    int vpH
) {
    if (screen_thermal == nullptr) return;

    // Adjust thermal image widget positioning and dimension if changed
    if (lv_obj_get_x(img_thermal_feed) != vpX || lv_obj_get_width(img_thermal_feed) != vpW) {
        lv_obj_set_pos(img_thermal_feed, vpX, vpY);
        lv_obj_set_size(img_thermal_feed, vpW, vpH);
        thermal_img_dsc.header.w = vpW;
        thermal_img_dsc.header.h = vpH;
        thermal_img_dsc.data_size = vpW * vpH * sizeof(lv_color_t);
        lv_img_set_src(img_thermal_feed, &thermal_img_dsc);

        if (vpX > 0) {
            lv_obj_clear_flag(hud_left_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(hud_right_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(hud_left_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(hud_right_panel, LV_OBJ_FLAG_HIDDEN);
        }

        // Align reticle with the optical center of the thermal viewport
        int cx = vpX + vpW / 2;
        int cy = vpY + vpH / 2;
        lv_obj_set_pos(thermal_reticle_container, cx - 7, cy - 7);
    }

    // Only update static colorbar canvas when palette actually changes
    static const uint16_t* lastRenderedPalette = nullptr;
    if (vpX > 0 && activePalette != nullptr && activePalette != lastRenderedPalette) {
        lastRenderedPalette = activePalette;
        for (int y = 0; y < 160; ++y) {
            int palIdx = 255 - (y * 255) / 159;
            lv_color_t c;
            c.full = activePalette[palIdx];
            for (int x = 0; x < 16; ++x) {
                colorbar_cbuf[y * 16 + x] = c;
            }
        }
        lv_obj_invalidate(canvas_colorbar);
    }

    bool isFrozen = (getAppState() == APP_STATE_THERMAL_FREEZE);
    static bool lastWasFrozen = false;
    bool stateChanged = (isFrozen != lastWasFrozen);
    lastWasFrozen = isFrozen;

    // Throttle label text glyph re-rasterization to ~7 Hz (140 ms), but update immediately on freeze/unfreeze
    static uint32_t lastHudTextUpdate = 0;
    uint32_t hudNow = millis();
    bool updateText = (hudNow - lastHudTextUpdate >= 140) || stateChanged;

    if (updateText) {
        lastHudTextUpdate = hudNow;

        if (vpX > 0) {
            if (title_thm != nullptr) {
                if (isFrozen) {
                    lv_label_set_text(title_thm, "[HOLD]");
                    lv_obj_set_style_text_color(title_thm, UI_COLOR_CYAN, 0);
                } else if (getThermalRangeModeSetting() == THERMAL_RANGE_MANUAL) {
                    lv_label_set_text(title_thm, "MANUAL");
                    lv_obj_set_style_text_color(title_thm, UI_COLOR_ACCENT, 0);
                } else {
                    lv_label_set_text(title_thm, "THERMAL");
                    lv_obj_set_style_text_color(title_thm, UI_COLOR_YELLOW, 0);
                }
            }
            if (label_hint_left != nullptr) {
                if (isFrozen) {
                    lv_label_set_text(label_hint_left, "[Click] Live");
                    lv_obj_set_style_text_color(label_hint_left, UI_COLOR_CYAN, 0);
                } else {
                    lv_label_set_text(label_hint_left, "[Click] Snap");
                    lv_obj_set_style_text_color(label_hint_left, UI_COLOR_TEXT_DIM, 0);
                }
            }
            if (label_hint_right != nullptr) {
                lv_label_set_text(label_hint_right, "[Hold] Menu");
                lv_obj_set_style_text_color(label_hint_right, UI_COLOR_TEXT_DIM, 0);
            }
            if (lbl_cnt != nullptr) lv_label_set_text(lbl_cnt, "CENTER");
            if (lbl_min_hdr != nullptr) lv_label_set_text(lbl_min_hdr, "MIN TEMP");
            if (lbl_max_hdr != nullptr) lv_label_set_text(lbl_max_hdr, "MAX TEMP");

            // Update Side Panels
            char buf[32];
            formatTempStr(buf, sizeof(buf), centerTemp, unit, 1, false);
            lv_label_set_text(label_center_temp, buf);

            lv_label_set_text(label_temp_unit, getUnitFullName(unit));

            formatTempStr(buf, sizeof(buf), sensorMinTemp, unit, 1, true);
            lv_label_set_text(label_min_temp, buf);

            formatTempStr(buf, sizeof(buf), sensorMaxTemp, unit, 1, true);
            lv_label_set_text(label_max_temp, buf);

            if (isFrozen) {
                lv_label_set_text(label_fps, "HOLD");
                lv_obj_set_style_text_color(label_fps, UI_COLOR_CYAN, 0);
            } else {
                snprintf(buf, sizeof(buf), "%.1f FPS", fps);
                lv_label_set_text(label_fps, buf);
                lv_obj_set_style_text_color(label_fps, UI_COLOR_GREEN, 0);
            }

            lv_label_set_text(label_scaling_mode, getScalingModeShortName(mode));

            formatTempStr(buf, sizeof(buf), colorMaxTemp, unit, 0, false);
            lv_label_set_text(label_colorbar_max, buf);

            float midTemp = 0.5f * (colorMinTemp + colorMaxTemp);
            formatTempStr(buf, sizeof(buf), midTemp, unit, 0, false);
            lv_label_set_text(label_colorbar_mid, buf);

            formatTempStr(buf, sizeof(buf), colorMinTemp, unit, 0, false);
            lv_label_set_text(label_colorbar_min, buf);
        }
    }

    // Invalidate thermal image so LVGL renders the newly upscaled frame
    lv_obj_invalidate(img_thermal_feed);
}

