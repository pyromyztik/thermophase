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
static lv_obj_t* label_fps = nullptr;
static lv_obj_t* label_scaling_mode = nullptr;
static lv_obj_t* canvas_colorbar = nullptr;
static lv_color_t colorbar_cbuf[16 * 160];
static lv_obj_t* label_colorbar_max = nullptr;
static lv_obj_t* label_colorbar_mid = nullptr;
static lv_obj_t* label_colorbar_min = nullptr;

// Fullscreen Overlays (for stretched mode)
static lv_obj_t* hud_fs_center_badge = nullptr;
static lv_obj_t* label_fs_center = nullptr;
static lv_obj_t* hud_fs_range_badge = nullptr;
static lv_obj_t* label_fs_min = nullptr;
static lv_obj_t* label_fs_max = nullptr;
static lv_obj_t* hud_fs_fps_badge = nullptr;
static lv_obj_t* label_fs_fps = nullptr;

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

    // 4. Side HUD Panel: RIGHT (FPS, Scaler Mode & Color Bar)
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

    label_fps = lv_label_create(hud_right_panel);
    lv_label_set_text(label_fps, "0.0 FPS");
    lv_obj_set_style_text_color(label_fps, UI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(label_fps, &lv_font_montserrat_10, 0);
    lv_obj_align(label_fps, LV_ALIGN_TOP_LEFT, 4, 6);

    label_scaling_mode = lv_label_create(hud_right_panel);
    lv_label_set_text(label_scaling_mode, "BICUB");
    lv_obj_set_style_text_color(label_scaling_mode, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_scaling_mode, &lv_font_montserrat_10, 0);
    lv_obj_align(label_scaling_mode, LV_ALIGN_TOP_LEFT, 4, 20);

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

    // 5. Fullscreen Overlay Badges (Hidden initially, shown when aspect ratio stretched)
    hud_fs_center_badge = lv_obj_create(screen_thermal);
    lv_obj_set_size(hud_fs_center_badge, 110, 22);
    lv_obj_align(hud_fs_center_badge, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(hud_fs_center_badge, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(hud_fs_center_badge, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(hud_fs_center_badge, 1, 0);
    lv_obj_set_style_radius(hud_fs_center_badge, 0, 0);
    lv_obj_clear_flag(hud_fs_center_badge, LV_OBJ_FLAG_SCROLLABLE);

    label_fs_center = lv_label_create(hud_fs_center_badge);
    lv_label_set_text(label_fs_center, "+ --.- " UI_SYMBOL_DEGREE "C");
    lv_obj_center(label_fs_center);
    lv_obj_set_style_text_color(label_fs_center, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_fs_center, &lv_font_montserrat_12, 0);

    hud_fs_range_badge = lv_obj_create(screen_thermal);
    lv_obj_set_size(hud_fs_range_badge, 160, 22);
    lv_obj_align(hud_fs_range_badge, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_set_style_bg_color(hud_fs_range_badge, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(hud_fs_range_badge, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(hud_fs_range_badge, 1, 0);
    lv_obj_set_style_radius(hud_fs_range_badge, 0, 0);
    lv_obj_clear_flag(hud_fs_range_badge, LV_OBJ_FLAG_SCROLLABLE);

    label_fs_min = lv_label_create(hud_fs_range_badge);
    lv_label_set_text(label_fs_min, "Min: --.- " UI_SYMBOL_DEGREE "C");
    lv_obj_align(label_fs_min, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_text_color(label_fs_min, UI_COLOR_CYAN, 0);
    lv_obj_set_style_text_font(label_fs_min, &lv_font_montserrat_10, 0);

    label_fs_max = lv_label_create(hud_fs_range_badge);
    lv_label_set_text(label_fs_max, "Max: --.- " UI_SYMBOL_DEGREE "C");
    lv_obj_align(label_fs_max, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_text_color(label_fs_max, UI_COLOR_ORANGE, 0);
    lv_obj_set_style_text_font(label_fs_max, &lv_font_montserrat_10, 0);

    hud_fs_fps_badge = lv_obj_create(screen_thermal);
    lv_obj_set_size(hud_fs_fps_badge, 90, 22);
    lv_obj_align(hud_fs_fps_badge, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_set_style_bg_color(hud_fs_fps_badge, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(hud_fs_fps_badge, UI_COLOR_GREEN, 0);
    lv_obj_set_style_border_width(hud_fs_fps_badge, 1, 0);
    lv_obj_set_style_radius(hud_fs_fps_badge, 0, 0);
    lv_obj_clear_flag(hud_fs_fps_badge, LV_OBJ_FLAG_SCROLLABLE);

    label_fs_fps = lv_label_create(hud_fs_fps_badge);
    lv_label_set_text(label_fs_fps, "--.- BICUB");
    lv_obj_center(label_fs_fps);
    lv_obj_set_style_text_color(label_fs_fps, UI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(label_fs_fps, &lv_font_montserrat_10, 0);

    // Hide fullscreen overlays by default (visible only if viewport is 320x240)
    lv_obj_add_flag(hud_fs_center_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hud_fs_range_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hud_fs_fps_badge, LV_OBJ_FLAG_HIDDEN);
}

void updateThermalHUD(
    float centerTemp,
    float minTemp,
    float maxTemp,
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
            lv_obj_add_flag(hud_fs_center_badge, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(hud_fs_range_badge, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(hud_fs_fps_badge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(hud_left_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(hud_right_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(hud_fs_center_badge, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(hud_fs_range_badge, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(hud_fs_fps_badge, LV_OBJ_FLAG_HIDDEN);
        }

        // Align reticle with the optical center of the thermal viewport
        int cx = vpX + vpW / 2;
        int cy = vpY + vpH / 2;
        lv_obj_set_pos(thermal_reticle_container, cx - 7, cy - 7);
    }

    const char* modeNames[5] = {"NEAR", "BILIN", "BICUB", "LANCZ", "TAAU"};
    const char* uSym = getUnitSym(unit);

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

    // Throttle label text glyph re-rasterization to ~7 Hz (140 ms).
    static uint32_t lastHudTextUpdate = 0;
    uint32_t hudNow = millis();
    bool updateText = (hudNow - lastHudTextUpdate >= 140);

    if (updateText) {
        lastHudTextUpdate = hudNow;

        if (vpX > 0) {
            if (title_thm != nullptr) {
                lv_label_set_text(title_thm, "THERMAL");
                lv_obj_set_style_text_color(title_thm, UI_COLOR_YELLOW, 0);
            }
            if (lbl_cnt != nullptr) lv_label_set_text(lbl_cnt, "CENTER");
            if (lbl_min_hdr != nullptr) lv_label_set_text(lbl_min_hdr, "MIN TEMP");
            if (lbl_max_hdr != nullptr) lv_label_set_text(lbl_max_hdr, "MAX TEMP");

            // Update Side Panels
            char buf[32];
            if (unit == TEMP_UNIT_KELVIN) {
                snprintf(buf, sizeof(buf), "%.1f", convertTemp(centerTemp, unit));
            } else {
                snprintf(buf, sizeof(buf), "%.1f" UI_SYMBOL_DEGREE, convertTemp(centerTemp, unit));
            }
            lv_label_set_text(label_center_temp, buf);

            lv_label_set_text(label_temp_unit, getUnitFullName(unit));

            snprintf(buf, sizeof(buf), "%.1f %s", convertTemp(minTemp, unit), uSym);
            lv_label_set_text(label_min_temp, buf);

            snprintf(buf, sizeof(buf), "%.1f %s", convertTemp(maxTemp, unit), uSym);
            lv_label_set_text(label_max_temp, buf);

            snprintf(buf, sizeof(buf), "%.1f FPS", fps);
            lv_label_set_text(label_fps, buf);

            lv_label_set_text(label_scaling_mode, modeNames[mode % 5]);

            if (unit == TEMP_UNIT_KELVIN) {
                snprintf(buf, sizeof(buf), "%.0f", convertTemp(maxTemp, unit));
            } else {
                snprintf(buf, sizeof(buf), "%.0f" UI_SYMBOL_DEGREE, convertTemp(maxTemp, unit));
            }
            lv_label_set_text(label_colorbar_max, buf);

            float midTemp = 0.5f * (minTemp + maxTemp);
            if (unit == TEMP_UNIT_KELVIN) {
                snprintf(buf, sizeof(buf), "%.0f", convertTemp(midTemp, unit));
            } else {
                snprintf(buf, sizeof(buf), "%.0f" UI_SYMBOL_DEGREE, convertTemp(midTemp, unit));
            }
            lv_label_set_text(label_colorbar_mid, buf);

            if (unit == TEMP_UNIT_KELVIN) {
                snprintf(buf, sizeof(buf), "%.0f", convertTemp(minTemp, unit));
            } else {
                snprintf(buf, sizeof(buf), "%.0f" UI_SYMBOL_DEGREE, convertTemp(minTemp, unit));
            }
            lv_label_set_text(label_colorbar_min, buf);

        } else {
            // Update Fullscreen Badges
            char buf[32];
            snprintf(buf, sizeof(buf), "+ %.1f %s", convertTemp(centerTemp, unit), uSym);
            lv_label_set_text(label_fs_center, buf);

            snprintf(buf, sizeof(buf), "Min:%.1f %s", convertTemp(minTemp, unit), uSym);
            lv_label_set_text(label_fs_min, buf);

            snprintf(buf, sizeof(buf), "Max:%.1f %s", convertTemp(maxTemp, unit), uSym);
            lv_label_set_text(label_fs_max, buf);

            snprintf(buf, sizeof(buf), "%.1f %s", fps, modeNames[mode % 5]);
            lv_label_set_text(label_fs_fps, buf);
        }
    }

    // Invalidate thermal image so LVGL renders the newly upscaled frame
    lv_obj_invalidate(img_thermal_feed);
}

