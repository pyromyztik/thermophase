#include "ui.hpp"

// Sensor Error Shared Variables
lv_obj_t* error_attempt_label = nullptr;
static uint32_t lastErrorAttempt = 0xFFFFFFFF;

void updateSensorErrorAttempt(uint32_t attemptCount) {
    if (attemptCount != lastErrorAttempt) {
        lastErrorAttempt = attemptCount;
        if (error_attempt_label != nullptr) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Auto-reconnecting... (Attempt #%u)", attemptCount);
            lv_label_set_text(error_attempt_label, buf);
        }
    }
}

void resetSensorErrorAttempt() {
    lastErrorAttempt = 0xFFFFFFFF;
}

void buildErrorScreen() {
    screen_error = lv_obj_create(NULL);
    lv_obj_add_style(screen_error, &style_screen, 0);
    lv_obj_clear_flag(screen_error, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_error, LV_SCROLLBAR_MODE_OFF);

    // Red Warning Outline
    lv_obj_t* border_box = lv_obj_create(screen_error);
    lv_obj_set_size(border_box, DISPLAY_WIDTH - 12, DISPLAY_HEIGHT - 12);
    lv_obj_center(border_box);
    lv_obj_set_style_bg_opa(border_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(border_box, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(border_box, 2, 0);
    lv_obj_set_style_radius(border_box, 0, 0);
    lv_obj_set_style_pad_all(border_box, 0, 0);
    lv_obj_clear_flag(border_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(border_box, LV_SCROLLBAR_MODE_OFF);

    // Red Title Banner (no padding, border, or scrollbar)
    lv_obj_t* banner = lv_obj_create(screen_error);
    lv_obj_set_size(banner, DISPLAY_WIDTH - 24, 26);
    lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(banner, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(banner, 0, 0);
    lv_obj_set_style_border_width(banner, 0, 0);
    lv_obj_set_style_pad_all(banner, 0, 0);
    lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(banner, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* banner_title = lv_label_create(banner);
    lv_label_set_text(banner_title, "THERMAL SENSOR ERROR");
    lv_obj_center(banner_title);
    lv_obj_set_style_text_color(banner_title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(banner_title, &lv_font_montserrat_14, 0);

    // Subtitle
    lv_obj_t* sub = lv_label_create(screen_error);
    lv_label_set_text(sub, "MLX90640 not responding on I2C bus (Addr 0x33)");
    lv_obj_set_style_text_color(sub, UI_COLOR_YELLOW, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 18, 44);

    // Pin Checklist
    lv_obj_t* pins = lv_label_create(screen_error);
    char pinBuf[128];
    snprintf(pinBuf, sizeof(pinBuf),
             "CHECK CONNECTIONS:\n  * SDA -> GPIO %d\n  * SCL -> GPIO %d\n  * VIN -> 3.3V (NOT 5V)\n  * GND -> Common GND",
             PIN_I2C_SDA, PIN_I2C_SCL);
    lv_label_set_text(pins, pinBuf);
    lv_obj_set_style_text_color(pins, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(pins, &lv_font_montserrat_10, 0);
    lv_obj_align(pins, LV_ALIGN_TOP_LEFT, 18, 62);

    // Auto Reconnect Label
    error_attempt_label = lv_label_create(screen_error);
    lv_label_set_text(error_attempt_label, "Auto-reconnecting... (Attempt #1)");
    lv_obj_set_style_text_color(error_attempt_label, UI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(error_attempt_label, &lv_font_montserrat_10, 0);
    lv_obj_align(error_attempt_label, LV_ALIGN_TOP_LEFT, 18, 140);
}

