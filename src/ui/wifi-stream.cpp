#include "ui.hpp"
#include "wifi.hpp"
#include "power.hpp"

// Wi-Fi Dedicated Streaming Screen Objects
lv_obj_t* screen_wifi_stream = nullptr;
static lv_obj_t* stream_stop_btn = nullptr;
static lv_obj_t* stream_stop_label = nullptr;
static lv_obj_t* stream_stop_arrow = nullptr;
static lv_obj_t* stream_client_label = nullptr;
static lv_obj_t* stream_stats_label = nullptr;
static lv_obj_t* stream_heap_label = nullptr;

static void stopStreamBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        if (stream_stop_arrow) lv_obj_clear_flag(stream_stop_arrow, LV_OBJ_FLAG_HIDDEN);
        if (stream_stop_label) lv_obj_set_style_text_color(stream_stop_label, lv_color_black(), 0);
        if (stream_stop_arrow) lv_obj_set_style_text_color(stream_stop_arrow, lv_color_black(), 0);
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (stream_stop_arrow) lv_obj_add_flag(stream_stop_arrow, LV_OBJ_FLAG_HIDDEN);
        if (stream_stop_label) lv_obj_set_style_text_color(stream_stop_label, UI_COLOR_RED, 0);
    } else if (code == LV_EVENT_CLICKED) {
        Serial.println("[UI] Stop Streaming button clicked! Rebooting device for clean standalone state...");
        if (stream_stop_label) lv_label_set_text(stream_stop_label, "RESTARTING CAMERA...");
        if (stream_heap_label) lv_label_set_text(stream_heap_label, "Rebooting into standard camera mode...");
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(350));
        ESP.restart();
    }
}

void buildWiFiStreamScreen() {
    screen_wifi_stream = lv_obj_create(NULL);
    lv_obj_add_style(screen_wifi_stream, &style_screen, 0);
    lv_obj_clear_flag(screen_wifi_stream, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_wifi_stream, LV_SCROLLBAR_MODE_OFF);

    // Header Title
    lv_obj_t* header = lv_label_create(screen_wifi_stream);
    lv_label_set_text(header, "WI-FI STREAMING");
    lv_obj_add_style(header, &style_header_title, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 14, 9);

    // Top Divider Line
    static lv_point_t h_line_pts[] = {{0, 32}, {DISPLAY_WIDTH - 1, 32}};
    lv_obj_t* line_top = lv_line_create(screen_wifi_stream);
    lv_line_set_points(line_top, h_line_pts, 2);
    lv_obj_add_style(line_top, &style_divider, 0);

    // Top-Right Battery Indicator
    createBatteryIndicator(screen_wifi_stream, true, LV_ALIGN_TOP_RIGHT, -14, 11);

    // Network Info Box (Card Container - Sharp 0px corners)
    lv_obj_t* card = lv_obj_create(screen_wifi_stream);
    lv_obj_set_pos(card, 16, 40);
    lv_obj_set_size(card, DISPLAY_WIDTH - 32, 116);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x121212), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_SEPARATOR, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // AP SSID & IP
    lv_obj_t* ap_label = lv_label_create(card);
    lv_label_set_text_fmt(ap_label, LV_SYMBOL_WIFI " AP: %s  |  %s", getWiFiAPSSID(), getWiFiAPIP());
    lv_obj_set_style_text_font(ap_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ap_label, UI_COLOR_CYAN, 0);
    lv_obj_align(ap_label, LV_ALIGN_TOP_LEFT, 0, 0);

    // Client connection status label
    stream_client_label = lv_label_create(card);
    lv_label_set_text(stream_client_label, "Client: Waiting for connection...");
    lv_obj_set_style_text_font(stream_client_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(stream_client_label, UI_COLOR_TEXT, 0);
    lv_obj_align(stream_client_label, LV_ALIGN_TOP_LEFT, 0, 24);

    // Live Streaming Telemetry (FPS, Temps)
    stream_stats_label = lv_label_create(card);
    lv_label_set_text(stream_stats_label, "FPS: --  |  Spot: --" UI_SYMBOL_DEGREE "C  |  [-- .. --]");
    lv_obj_set_style_text_font(stream_stats_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(stream_stats_label, UI_COLOR_GREEN, 0);
    lv_obj_align(stream_stats_label, LV_ALIGN_TOP_LEFT, 0, 50);

    // Memory & Pipeline State
    stream_heap_label = lv_label_create(card);
    lv_label_set_text(stream_heap_label, "Status: Dedicated Low-Memory Mode (Display Paused)");
    lv_obj_set_style_text_font(stream_heap_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(stream_heap_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(stream_heap_label, LV_ALIGN_TOP_LEFT, 0, 78);

    // Prominent Stop Streaming Button (Sharp 0px corners + restart warning)
    stream_stop_btn = lv_btn_create(screen_wifi_stream);
    lv_obj_set_size(stream_stop_btn, 240, 36);
    lv_obj_align(stream_stop_btn, LV_ALIGN_TOP_MID, 0, 168);
    lv_obj_add_style(stream_stop_btn, &style_btn_default, 0);
    lv_obj_add_style(stream_stop_btn, &style_btn_back_focused, LV_STATE_FOCUSED);
    lv_obj_add_style(stream_stop_btn, &style_btn_back_focused, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(stream_stop_btn, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(stream_stop_btn, 0, 0);
    lv_obj_set_style_radius(stream_stop_btn, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(stream_stop_btn, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_radius(stream_stop_btn, 0, LV_STATE_PRESSED);

    // Stop Arrow
    stream_stop_arrow = lv_label_create(stream_stop_btn);
    lv_label_set_text(stream_stop_arrow, LV_SYMBOL_STOP);
    lv_obj_set_style_text_font(stream_stop_arrow, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(stream_stop_arrow, lv_color_black(), 0);
    lv_obj_align(stream_stop_arrow, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_add_flag(stream_stop_arrow, LV_OBJ_FLAG_HIDDEN);

    // Stop Label with Restart Warning
    stream_stop_label = lv_label_create(stream_stop_btn);
    lv_label_set_text(stream_stop_label, "STOP & RESTART CAMERA");
    lv_obj_set_style_text_font(stream_stop_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(stream_stop_label, UI_COLOR_RED, 0);
    lv_obj_align(stream_stop_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(stream_stop_btn, stopStreamBtnEventCb, LV_EVENT_ALL, NULL);

    // Bottom Divider Line
    static lv_point_t f_line_pts[] = {{0, 216}, {DISPLAY_WIDTH - 1, 216}};
    lv_obj_t* line_bot = lv_line_create(screen_wifi_stream);
    lv_line_set_points(line_bot, f_line_pts, 2);
    lv_obj_add_style(line_bot, &style_divider, 0);

    // Footer Hint with Explicit Restart Warning
    lv_obj_t* footer = lv_label_create(screen_wifi_stream);
    lv_label_set_text(footer, "[Click Wheel] Stop Stream (Device will Restart)");
    lv_obj_add_style(footer, &style_footer_text, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 14, -8);
}

void uiNavToWiFiStream() {
    if (!screen_wifi_stream) buildWiFiStreamScreen();

    setAppState(APP_STATE_WIFI_STREAM);
    setWiFiStreamingActive(true);

    if (ui_wheel_group) {
        lv_group_remove_all_objs(ui_wheel_group);
        if (stream_stop_btn) {
            lv_group_add_obj(ui_wheel_group, stream_stop_btn);
            lv_group_focus_obj(stream_stop_btn);
        }
    }

    lv_scr_load_anim(screen_wifi_stream, LV_SCR_LOAD_ANIM_FADE_ON, 120, 0, false);
    Serial.println("[UI] Entered dedicated Wi-Fi Streaming mode (120ms Fade).");
}

void updateWiFiStreamHUD(float spotTemp, float minTemp, float maxTemp, float fps, uint32_t freePsram, const char* clientIp) {
    if (getAppState() != APP_STATE_WIFI_STREAM || lv_scr_act() != screen_wifi_stream) {
        return;
    }

    // Update Client Status
    if (stream_client_label) {
        uint8_t count = getWiFiConnectedStationCount();
        if (count > 0) {
            char buf[96];
            if (clientIp && clientIp[0] && strcmp(clientIp, "None") != 0) {
                snprintf(buf, sizeof(buf), "Stream: Live to %s (%u active)", clientIp, count);
                lv_obj_set_style_text_color(stream_client_label, UI_COLOR_GREEN, 0);
            } else {
                snprintf(buf, sizeof(buf), "Network: Station Connected (%u) | Ready to Stream", count);
                lv_obj_set_style_text_color(stream_client_label, UI_COLOR_CYAN, 0);
            }
            lv_label_set_text(stream_client_label, buf);
        } else {
            lv_label_set_text(stream_client_label, "Client: Waiting for connection...");
            lv_obj_set_style_text_color(stream_client_label, UI_COLOR_TEXT_DIM, 0);
        }
    }

    // Update Telemetry
    if (stream_stats_label) {
        char buf[96];
        snprintf(buf, sizeof(buf), "FPS: %.1f  |  Spot: %.1f" UI_SYMBOL_DEGREE "C  |  [%.1f .. %.1f]",
                 fps, spotTemp, minTemp, maxTemp);
        lv_label_set_text(stream_stats_label, buf);
    }

    // Update Memory State
    if (stream_heap_label) {
        char buf[96];
        uint32_t freeInternal = esp_get_free_internal_heap_size();
        snprintf(buf, sizeof(buf), "Internal RAM: %u KB free  |  PSRAM: %u KB free",
                 (unsigned int)(freeInternal / 1024), (unsigned int)(freePsram / 1024));
        lv_label_set_text(stream_heap_label, buf);
    }
}

