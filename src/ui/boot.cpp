#include "ui.hpp"

// Boot Screen Widgets
static lv_obj_t* boot_bar = nullptr;
static lv_obj_t* boot_status_label = nullptr;
static lv_obj_t* boot_pct_label = nullptr;
static lv_obj_t* boot_reticle_ring = nullptr;

static void anim_pulse_cb(void* var, int32_t v) {
    lv_obj_set_style_outline_pad((lv_obj_t*)var, v, 0);
}

void buildBootScreen() {
    screen_boot = lv_obj_create(NULL);
    lv_obj_add_style(screen_boot, &style_screen, 0);
    lv_obj_clear_flag(screen_boot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_boot, LV_SCROLLBAR_MODE_OFF);

    // Aperture / Reticle Icon Emblem at (160, 52)
    boot_reticle_ring = lv_obj_create(screen_boot);
    lv_obj_set_size(boot_reticle_ring, 44, 44);
    lv_obj_align(boot_reticle_ring, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_radius(boot_reticle_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(boot_reticle_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(boot_reticle_ring, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(boot_reticle_ring, 2, 0);
    lv_obj_set_style_outline_color(boot_reticle_ring, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_outline_opa(boot_reticle_ring, LV_OPA_60, 0);

    // Animated breathing ring on aperture icon
    lv_anim_t a_ring;
    lv_anim_init(&a_ring);
    lv_anim_set_var(&a_ring, boot_reticle_ring);
    lv_anim_set_values(&a_ring, 0, 4);
    lv_anim_set_time(&a_ring, 900);
    lv_anim_set_playback_time(&a_ring, 900);
    lv_anim_set_repeat_count(&a_ring, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a_ring, anim_pulse_cb);
    lv_anim_start(&a_ring);

    // Inner bright center core
    lv_obj_t* core = lv_obj_create(boot_reticle_ring);
    lv_obj_set_size(core, 12, 12);
    lv_obj_center(core);
    lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(core, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(core, 0, 0);

    // Title: "THERMOPHASE"
    lv_obj_t* title = lv_label_create(screen_boot);
    lv_label_set_text(title, "THERMOPHASE");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 88);

    // Subtitle: "REAL-TIME THERMAL IMAGER"
    lv_obj_t* subtitle = lv_label_create(screen_boot);
    lv_label_set_text(subtitle, "REAL-TIME THERMAL IMAGER");
    lv_obj_set_style_text_color(subtitle, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_letter_space(subtitle, 1, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 114);

    // Progress Bar
    boot_bar = lv_bar_create(screen_boot);
    lv_obj_set_size(boot_bar, 220, 6);
    lv_obj_align(boot_bar, LV_ALIGN_TOP_MID, 0, 168);
    lv_obj_set_style_bg_color(boot_bar, UI_COLOR_BG, 0);
    lv_obj_set_style_border_color(boot_bar, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(boot_bar, 1, 0);
    lv_obj_set_style_radius(boot_bar, 2, 0);
    lv_obj_set_style_bg_color(boot_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(boot_bar, 2, LV_PART_INDICATOR);
    lv_bar_set_range(boot_bar, 0, 100);
    lv_bar_set_value(boot_bar, 0, LV_ANIM_OFF);

    // Status Message below bar
    boot_status_label = lv_label_create(screen_boot);
    lv_label_set_text(boot_status_label, "Initializing...");
    lv_obj_set_style_text_color(boot_status_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(boot_status_label, &lv_font_montserrat_10, 0);
    lv_obj_align(boot_status_label, LV_ALIGN_TOP_LEFT, 50, 184);

    // Percentage on right
    boot_pct_label = lv_label_create(screen_boot);
    lv_label_set_text(boot_pct_label, "0%");
    lv_obj_set_style_text_color(boot_pct_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(boot_pct_label, &lv_font_montserrat_10, 0);
    lv_obj_align(boot_pct_label, LV_ALIGN_TOP_RIGHT, -50, 184);
}

void updateBootScreenUI(const char* statusText, uint8_t progressPercent) {
    if (boot_bar == nullptr) return;
    if (progressPercent > 100) progressPercent = 100;

    lv_bar_set_value(boot_bar, progressPercent, LV_ANIM_ON);
    if (statusText != nullptr) {
        lv_label_set_text(boot_status_label, statusText);
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%u%%", progressPercent);
    lv_label_set_text(boot_pct_label, buf);

    // Force LVGL timer update to render immediately during blocking boot steps
    lv_refr_now(NULL);
}

