#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>
#include <esp_heap_caps.h>
#include "ui_fonts.h"
#include "live_data.h"
#include "dashboard.h"
#include "saved_settings.h"

namespace {

constexpr int TOUCH_CS = 33;
// Provisional rotation-1 mapping; verify alignment on the actual hardware.
constexpr int TOUCH_RAW_MIN = 200, TOUCH_RAW_MAX = 3900;
constexpr uint32_t CONNECT_TIMEOUT = 20000;
TFT_eSPI tft;
SPIClass touch_spi(VSPI);
// Poll pressure rather than relying on the provisional IRQ wiring.
XPT2046_Touchscreen touch(TOUCH_CS);
enum class Screen { CALIBRATION, WIFI, DASHBOARD };
Screen screen = Screen::CALIBRATION;
constexpr int CORNER_X[] = {25, 294, 294, 25};
constexpr int CORNER_Y[] = {25, 25, 214, 214};
constexpr int TARGET_SPAN_X = CORNER_X[1] - CORNER_X[0];
constexpr int TARGET_SPAN_Y = CORNER_Y[3] - CORNER_Y[0];
constexpr int TARGET_RADIUS = 9;
// Corner identity needs a known orientation: rotation 1, raw x/right and y/down.
constexpr int RAW_CORNER_LOW = TOUCH_RAW_MIN + (TOUCH_RAW_MAX - TOUCH_RAW_MIN) / 4;
constexpr int RAW_CORNER_HIGH = TOUCH_RAW_MAX - (TOUCH_RAW_MAX - TOUCH_RAW_MIN) / 4;
struct Calibration {
    float ox, oy, ux, uy, vx, vy;
    bool valid;
} calibration{};
bool corner_done[4]{};
float corner_raw_x[4]{}, corner_raw_y[4]{};
bool calibration_complete = false, calibration_ignore_hold = false;
uint32_t calibration_completed_at = 0;
int sample_corner = -1, sample_count = 0, release_samples = 0;
long sample_sum_x = 0, sample_sum_y = 0;
int sample_min_x = 4095, sample_max_x = 0, sample_min_y = 4095, sample_max_y = 0;
bool sample_rejected = false;
bool pressed_before = false;
bool touch_diagnostics = false;
uint32_t last_touch_diagnostic = 0;
uint32_t last_touch = 0, last_animation = 0, connect_started = 0;
uint8_t page = 0, spinner_phase = 0;
bool connecting = false, password_field = false, uppercase = false, symbols = false;
uint8_t symbol_page = 0;
constexpr uint16_t WIFI_BG = TFT_BLACK, WIFI_KEY = 0x2104, WIFI_MUTED = 0xAD55;
constexpr int FIELD_Y[] = {36, 69};
constexpr int FIELD_H = 29, KEYBOARD_Y = 121, KEY_ROW_STEP = 31, KEY_H = 29;
struct KeyRect { int x, y, w, h; };
String ssid, password;
const char *wifi_message = "";
bool dashboard_touch_armed = false;


bool inside(int x, int y, int bx, int by, int w, int h) {
    return x >= bx && x < bx + w && y >= by && y < by + h;
}

void ui_text(const char *s, int x, int y, const GFXfont *font = &nunito_18,
             uint16_t color = TFT_WHITE, uint16_t bg = TFT_BLACK, uint8_t datum = TL_DATUM) {
    tft.setFreeFont(font);
    tft.setTextColor(color, bg);
    tft.setTextDatum(datum);
    tft.drawString(s, x, y, 1);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
}
void ui_button(int x, int y, int w, int h, const char *label) {
    tft.fillRoundRect(x, y, w, h, 8, WIFI_KEY);
    ui_text(label, x + w / 2, y + h / 2, &nunito_18, TFT_WHITE, WIFI_KEY, MC_DATUM);
}
void rounded_stroke(int x1, int y1, int x2, int y2, uint16_t color, int radius = 3) {
    int steps = max(abs(x2 - x1), abs(y2 - y1));
    for (int i = 0; i <= steps; ++i)
        tft.fillCircle(x1 + (x2 - x1) * i / max(steps, 1), y1 + (y2 - y1) * i / max(steps, 1), radius, color);
}
void draw_corner_target(int corner) {
    // Targets retain canonical rotation-1 identities for old and newly saved fits.
        int x = 319 - CORNER_X[corner], y = 239 - CORNER_Y[corner];
    int sx = x < 160 ? 1 : -1, sy = y < 120 ? 1 : -1;
    uint16_t color = corner_done[corner] ? TFT_GREEN : TFT_WHITE;
    tft.drawCircle(x, y, TARGET_RADIUS, color);
    tft.drawCircle(x, y, TARGET_RADIUS - 1, color);
    tft.fillCircle(x, y, 2, color);
    // The small arrow stops outside the ring; only the center dot is the fit target.
    int tip_x = x + sx * 12, tip_y = y + sy * 12;
    rounded_stroke(tip_x, tip_y, x + sx * 27, y + sy * 27, TFT_WHITE, 2);
    rounded_stroke(tip_x, tip_y, x + sx * 22, tip_y, TFT_WHITE, 2);
    rounded_stroke(tip_x, tip_y, tip_x, y + sy * 22, TFT_WHITE, 2);
}
void draw_calibration() {
    tft.fillScreen(TFT_BLACK);
    for (int i = 0; i < 4; ++i) draw_corner_target(i);
    ui_text("Click center", 160, 105, &nunito_26, TFT_WHITE, TFT_BLACK, MC_DATUM);
    ui_text("of circles", 160, 137, &nunito_26, TFT_WHITE, TFT_BLACK, MC_DATUM);
}
uint32_t calibration_profile() {
    // Fingerprint canonical touch geometry, not display rotation: preserve existing NVS fits.
    const int geometry[] = {320, 240, 1, TOUCH_CS, 25, 39, 32, TOUCH_RAW_MIN, TOUCH_RAW_MAX,
        CORNER_X[0], CORNER_X[1], CORNER_X[2], CORNER_X[3],
        CORNER_Y[0], CORNER_Y[1], CORNER_Y[2], CORNER_Y[3]};
    uint32_t profile = 2166136261U;
    for (int value : geometry) profile = (profile ^ uint32_t(value)) * 16777619U;
    return profile;
}
void reset_corner_sample() {
    sample_corner = -1;
    sample_count = release_samples = 0;
    sample_sum_x = sample_sum_y = 0;
    sample_min_x = sample_min_y = 4095;
    sample_max_x = sample_max_y = 0;
    sample_rejected = false;
}
void start_calibration() {
    dashboard::hide();
    dashboard_touch_armed = false;
    WiFi.disconnect(false, false);
    connecting = false;
    screen = Screen::CALIBRATION;
    calibration.valid = false;
    calibration_complete = false;
    calibration_ignore_hold = true;
    pressed_before = false;
    for (int i = 0; i < 4; ++i) corner_done[i] = false;
    reset_corner_sample();
    draw_calibration();
}
int raw_corner(int x, int y) {
    bool left = x <= RAW_CORNER_LOW, right = x >= RAW_CORNER_HIGH;
    bool top = y <= RAW_CORNER_LOW, bottom = y >= RAW_CORNER_HIGH;
    if ((!left && !right) || (!top && !bottom)) return -1;
    return top ? (left ? 0 : 1) : (left ? 3 : 2);
}
bool fit_calibration() {
    Calibration candidate{
        (3 * corner_raw_x[0] + corner_raw_x[1] - corner_raw_x[2] + corner_raw_x[3]) / 4,
        (3 * corner_raw_y[0] + corner_raw_y[1] - corner_raw_y[2] + corner_raw_y[3]) / 4,
        (corner_raw_x[1] + corner_raw_x[2] - corner_raw_x[0] - corner_raw_x[3]) / 2,
        (corner_raw_y[1] + corner_raw_y[2] - corner_raw_y[0] - corner_raw_y[3]) / 2,
        (corner_raw_x[3] + corner_raw_x[2] - corner_raw_x[0] - corner_raw_x[1]) / 2,
        (corner_raw_y[3] + corner_raw_y[2] - corner_raw_y[0] - corner_raw_y[1]) / 2,
        true};
    float det = candidate.ux * candidate.vy - candidate.uy * candidate.vx;
    float closure = hypotf(corner_raw_x[0] + corner_raw_x[2] - corner_raw_x[1] - corner_raw_x[3],
        corner_raw_y[0] + corner_raw_y[2] - corner_raw_y[1] - corner_raw_y[3]);
    if (!isfinite(det) || fabsf(det) < 1000000 || closure > 180 ||
        hypotf(candidate.ux, candidate.uy) < 1800 || hypotf(candidate.vx, candidate.vy) < 1800) {
        Serial.printf("Calibration rejected: det=%.0f closure=%.1f\n", det, closure);
        return false;
    }
    // Check the fitted screen error at every corner, not just raw parallelogram closure.
    for (int i = 0; i < 4; ++i) {
        float dx = corner_raw_x[i] - candidate.ox, dy = corner_raw_y[i] - candidate.oy;
        float x = CORNER_X[0] + TARGET_SPAN_X * (dx * candidate.vy - dy * candidate.vx) / det;
        float y = CORNER_Y[0] + TARGET_SPAN_Y * (candidate.ux * dy - candidate.uy * dx) / det;
        float residual = hypotf(x - CORNER_X[i], y - CORNER_Y[i]);
        Serial.printf("Calibration corner %d: target=%d,%d fitted=%.1f,%.1f residual=%.2fpx\n",
            i, CORNER_X[i], CORNER_Y[i], x, y, residual);
        if (residual > 6) return false;
    }
    calibration = candidate;
    Serial.printf("Calibration fit: origin=%.2f,%.2f U=%.2f,%.2f V=%.2f,%.2f det=%.0f closure=%.1f\n",
        candidate.ox, candidate.oy, candidate.ux, candidate.uy, candidate.vx, candidate.vy, det, closure);
    return true;
}
void accept_corner(uint32_t now) {
    if (sample_rejected || sample_count < 4 || sample_corner < 0 || corner_done[sample_corner]) {
        Serial.printf("Calibration ignored: corner=%d samples=%d rejected=%d spread=%d,%d\n",
            sample_corner, sample_count, sample_rejected,
            sample_count ? sample_max_x - sample_min_x : 0, sample_count ? sample_max_y - sample_min_y : 0);
        return;
    }
    corner_raw_x[sample_corner] = float(sample_sum_x) / sample_count;
    corner_raw_y[sample_corner] = float(sample_sum_y) / sample_count;
    bool final_corner = true;
    for (int i = 0; i < 4; ++i) if (i != sample_corner && !corner_done[i]) final_corner = false;
    if (final_corner && !fit_calibration()) {
        for (int i = 0; i < 4; ++i) corner_done[i] = false;
        calibration.valid = false;
        Serial.println("Calibration: invalid geometry; all markers reset. Tap circle center dots accurately.");
        draw_calibration();
        return;
    }
    corner_done[sample_corner] = true;
    draw_corner_target(sample_corner);
    Serial.printf("Calibration corner %d accepted: raw %.0f,%.0f\n", sample_corner,
        corner_raw_x[sample_corner], corner_raw_y[sample_corner]);
    if (final_corner) {
        const saved_settings::Affine fitted = {calibration.ox, calibration.oy, calibration.ux,
            calibration.uy, calibration.vx, calibration.vy};
        if (!saved_settings::save_calibration(fitted, calibration_profile()))
            Serial.println("Calibration NVS save failed; usable this boot only.");
        calibration_complete = true;
        calibration_completed_at = now;
    }
}
void poll_calibration(const TS_Point &point, bool raw_pressed, uint32_t now) {
    if (calibration_complete) return;
    if (!raw_pressed) {
        if (++release_samples < 3) return;
        if (!calibration_ignore_hold && (sample_count || sample_rejected)) accept_corner(now);
        calibration_ignore_hold = false;
        reset_corner_sample();
        return;
    }
    release_samples = 0;
    if (calibration_ignore_hold) return;
    int corner = raw_corner(point.x, point.y);
    if (corner < 0 || corner_done[corner] || (sample_corner >= 0 && corner != sample_corner)) {
        sample_rejected = true;
        return;
    }
    sample_corner = corner;
    sample_min_x = min(sample_min_x, int(point.x)); sample_max_x = max(sample_max_x, int(point.x));
    sample_min_y = min(sample_min_y, int(point.y)); sample_max_y = max(sample_max_y, int(point.y));
    if (sample_max_x - sample_min_x > 100 || sample_max_y - sample_min_y > 100) sample_rejected = true;
    if (sample_count < 12) {
        sample_sum_x += point.x; sample_sum_y += point.y; ++sample_count;
    }
}
bool map_touch(int rx, int ry, int &x, int &y) {
    if (!calibration.valid) return false;
    float dx = rx - calibration.ox, dy = ry - calibration.oy;
    float det = calibration.ux * calibration.vy - calibration.uy * calibration.vx;
    if (rx <= 0 || rx >= 4095 || ry <= 0 || ry >= 4095) return false;
    x = lroundf(CORNER_X[0] + TARGET_SPAN_X * (dx * calibration.vy - dy * calibration.vx) / det);
    y = lroundf(CORNER_Y[0] + TARGET_SPAN_Y * (calibration.ux * dy - calibration.uy * dx) / det);
    // Inset targets require extrapolation. Do not discard valid ADC touches near the edges.
    x = constrain(x, 0, 319);
    y = constrain(y, 0, 239);
    // Raw touch and persisted affine fit stay rotation 1; rotate only final UI coordinates.
    x = 319 - x;
    y = 239 - y;
    return true;
}

const char *keyboard_row(int row) {
    static const char *lower[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const char *upper[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    static const char *numbers[] = {"1234567890", "!@#$%^&*()", "-_=+.,?/;:"};
    static const char *special[] = {"[]{}<>\\|~`", "'\"", ""};
    return symbols ? (symbol_page == 0 ? numbers[row] : special[row]) : (uppercase ? upper[row] : lower[row]);
}
KeyRect keyboard_key(int row, int col) {
    int count = strlen(keyboard_row(row));
    if (count == 0) return {0, 0, 0, 0};
    int width = min(44, (316 - (count - 1) * 2) / count);
    int start = (320 - (count * width + (count - 1) * 2)) / 2;
    return {start + col * (width + 2), KEYBOARD_Y + row * KEY_ROW_STEP, width, KEY_H};
}
void append_character(char character) {
    String &value = password_field ? password : ssid;
    if (value.length() < (password_field ? 63U : 32U)) {
        value += character;
        wifi_message = "";
    } else wifi_message = password_field ? "Password full (63)" : "SSID full (32)";
}
void draw_fields() {
    for (int i = 0; i < 2; ++i) {
        int y = FIELD_Y[i];
        bool active = password_field == (i == 1);
        tft.fillRoundRect(4, y, 312, FIELD_H, 8, WIFI_KEY);
        tft.drawRoundRect(4, y, 312, FIELD_H, 8, active ? TFT_WHITE : WIFI_KEY);
        ui_text(i == 0 ? "SSID" : "Password", 12, y + FIELD_H / 2, &nunito_18,
                WIFI_MUTED, WIFI_KEY, ML_DATUM);
        String value = i == 0 ? ssid : password;
        tft.setFreeFont(&nunito_18);
        bool trimmed = false;
        while (!value.isEmpty() && tft.textWidth((trimmed ? "<" + value : value).c_str(), 1) > 198) {
            value.remove(0, 1);
            trimmed = true;
        }
        if (trimmed) value = "<" + value;
        ui_text(value.c_str(), 106, y + FIELD_H / 2, &nunito_18, TFT_WHITE, WIFI_KEY, ML_DATUM);
    }
}
void draw_wifi_status() {
    tft.fillRect(0, 99, 320, 21, WIFI_BG);
    if (wifi_message[0]) ui_text(wifi_message, 160, 109, &nunito_18, TFT_WHITE, WIFI_BG, MC_DATUM);
}
void draw_keyboard() {
    tft.fillRect(0, KEYBOARD_Y, 320, 240 - KEYBOARD_Y, WIFI_BG);
    for (int row = 0; row < 3; ++row) {
        const char *keys = keyboard_row(row);
        for (int col = 0; col < int(strlen(keys)); ++col) {
            char key[] = {keys[col], '\0'};
            KeyRect rect = keyboard_key(row, col);
            ui_button(rect.x, rect.y, rect.w, rect.h, key);
        }
    }
    ui_button(4, 214, 49, 26, symbols ? (symbol_page == 0 ? "#+=" : "123") : (uppercase ? "abc" : "ABC"));
    ui_button(56, 214, 50, 26, symbols ? "ABC" : "123");
    ui_button(109, 214, 142, 26, "Space");
    ui_button(254, 214, 62, 26, "Del");
}
void draw_wifi() {
    tft.fillScreen(WIFI_BG);
    ui_text("WiFi", 12, 16, &nunito_26, TFT_WHITE, WIFI_BG, ML_DATUM);
    ui_button(218, 3, 98, 29, connecting ? "Cancel" : "Connect");
    draw_fields();
    draw_wifi_status();
    if (connecting) {
        last_animation = 0;
    } else draw_keyboard();
}
void show_gate(const char *message) {
    dashboard::hide();
    screen = Screen::WIFI;
    connecting = false;
    wifi_message = message;
    dashboard_touch_armed = false;
    draw_wifi();
}
void begin_connect() {
    if (ssid.isEmpty()) {
        wifi_message = "Enter SSID";
        draw_wifi_status();
        return;
    }
    if (!password.isEmpty() && password.length() < 8) {
        wifi_message = "Password: 8+ or empty";
        draw_wifi_status();
        return;
    }
    WiFi.disconnect(false, false);
    WiFi.begin(ssid.c_str(), password.isEmpty() ? nullptr : password.c_str());
    connecting = true;
    connect_started = millis();
    spinner_phase = 0;
    wifi_message = "Connecting...";
    draw_wifi();
}
void enter_wifi() {
    saved_settings::Credentials stored;
    const bool saved = saved_settings::load_wifi(stored);
    if (saved) { ssid = stored.ssid; password = stored.password; }
    show_gate("");
    if (saved) begin_connect(); // Existing spinner/Cancel/20s timeout, no blocking wait.
}
void restore_startup() {
    saved_settings::Affine stored;
    if (saved_settings::load_calibration(stored, calibration_profile())) {
        calibration = {stored.ox, stored.oy, stored.ux, stored.uy, stored.vx, stored.vy, true};
        pressed_before = false;
        dashboard_touch_armed = false;
        enter_wifi();
    } else start_calibration(); // Missing/corrupt/incompatible record: original circle workflow.
}
void force_recalibration() {
    if (!saved_settings::forget_calibration())
        Serial.println("Calibration NVS erase failed; saved mapping may remain after reboot.");
    start_calibration(); // Never erase the independent WiFi record.
}
void forget_saved_wifi() {
    const bool forgotten = saved_settings::forget_wifi();
    WiFi.disconnect(false, false);
    connecting = false;
    ssid = ""; password = "";
    if (!forgotten) Serial.println("WiFi NVS erase failed; saved credentials may remain after reboot.");
    if (calibration.valid) show_gate(forgotten ? "WiFi forgotten" : "NVS erase failed");
    else Serial.println("Finish calibration before the WiFi form.");
}
void wifi_tap(int x, int y) {
    if (connecting) {
        if (inside(x, y, 218, 3, 98, 29)) {
            WiFi.disconnect(false, false);
            show_gate("");
        }
        return;
    }
    if (inside(x, y, 218, 3, 98, 29)) { begin_connect(); return; }
    if (inside(x, y, 4, FIELD_Y[0], 312, FIELD_H) || inside(x, y, 4, FIELD_Y[1], 312, FIELD_H)) {
        password_field = y >= FIELD_Y[1];
        draw_fields();
        return;
    }
    for (int row = 0; row < 3; ++row) {
        const char *keys = keyboard_row(row);
        for (int col = 0; col < int(strlen(keys)); ++col) {
            KeyRect rect = keyboard_key(row, col);
            if (inside(x, y, rect.x, rect.y, rect.w, rect.h)) {
                if (touch_diagnostics) Serial.println("Touch: keyboard hit (character not logged).");
                append_character(keys[col]);
                draw_fields();
                draw_wifi_status();
                return;
            }
        }
    }
    if (inside(x, y, 4, 214, 49, 26)) {
        if (symbols) symbol_page = (symbol_page + 1) % 2;
        else uppercase = !uppercase;
        draw_keyboard();
    } else if (inside(x, y, 56, 214, 50, 26)) {
        symbols = !symbols;
        symbol_page = 0;
        draw_keyboard();
    } else if (inside(x, y, 109, 214, 142, 26)) {
        append_character(' ');
        draw_fields();
        draw_wifi_status();
    } else if (inside(x, y, 254, 214, 62, 26)) {
        String &value = password_field ? password : ssid;
        if (!value.isEmpty()) value.remove(value.length() - 1);
        wifi_message = "";
        draw_fields();
        draw_wifi_status();
    } else if (touch_diagnostics) Serial.printf("Touch: no WiFi hit at %d,%d\n", x, y);
}

void draw_dashboard() {
    if (WiFi.status() != WL_CONNECTED) { show_gate("WiFi lost. Reconnect."); return; }
    screen = Screen::DASHBOARD;
    if (!dashboard::begin(tft)) { show_gate("UI memory unavailable"); return; }
    dashboard::show(page);
}
void next_page() {
    if (screen == Screen::DASHBOARD && WiFi.status() == WL_CONNECTED) dashboard::next();
}
void poll_touch(uint32_t now) {
    if (now - last_touch < 20) return;
    last_touch = now;
    TS_Point point = touch.getPoint();
    bool raw_pressed = point.z >= 350 && point.x > 0 && point.x < 4095 && point.y > 0 && point.y < 4095;
    if (touch_diagnostics && point.z > 0 && now - last_touch_diagnostic >= 100) {
        last_touch_diagnostic = now;
        int x = -1, y = -1;
        bool mapped = map_touch(point.x, point.y, x, y);
        // Coordinates/pressure only; never print characters, SSID or password.
        Serial.printf("Touch raw=%d,%d z=%d pressure_ok=%d mapped=%d xy=%d,%d down=%d\n",
            point.x, point.y, point.z, raw_pressed, mapped, x, y, pressed_before);
    }
    static uint8_t down_samples = 0, up_samples = 0;
    if (screen == Screen::CALIBRATION) {
        down_samples = up_samples = 0;
        pressed_before = false;
        poll_calibration(point, raw_pressed, now);
        return;
    }
    static int previous_x = 0, previous_y = 0;
    bool pressed = pressed_before;
    if (raw_pressed) {
        up_samples = 0;
        if (screen == Screen::WIFI && down_samples &&
            (abs(point.x - previous_x) > 100 || abs(point.y - previous_y) > 100)) down_samples = 0;
        down_samples = min(3, int(down_samples) + 1);
        previous_x = point.x;
        previous_y = point.y;
        if (down_samples >= 2) pressed = true;
    } else {
        down_samples = 0;
        up_samples = min(3, int(up_samples) + 1);
        if (up_samples >= 3) pressed = false;
    }
    // Arm only after a full release on the dashboard. A Connect hold cannot turn a page.
    if (screen == Screen::DASHBOARD && !pressed && up_samples >= 3) dashboard_touch_armed = true;
    // A single low-pressure read must not release a held key.
    if (pressed && !pressed_before) {
        int x, y;
        if (map_touch(point.x, point.y, x, y)) {
            if (screen == Screen::WIFI) wifi_tap(x, y);
        }
    }
    if (screen == Screen::DASHBOARD) {
        static int dashboard_x = 0, dashboard_y = 0;
        if (raw_pressed) map_touch(point.x, point.y, dashboard_x, dashboard_y);
        dashboard::touch(pressed && dashboard_touch_armed, dashboard_x, dashboard_y);
    }
    pressed_before = pressed;
}
void update_network(uint32_t now) {
    if (screen == Screen::CALIBRATION) {
        if (calibration_complete && now - calibration_completed_at >= 600) enter_wifi();
        return;
    }
    wl_status_t status = WiFi.status();
    if (screen == Screen::DASHBOARD && status != WL_CONNECTED) {
        WiFi.disconnect(false, false);
        show_gate("WiFi lost. Reconnect.");
    } else if (connecting) {
        if (status == WL_CONNECTED && WiFi.SSID() == ssid) {
            // Fields cannot be edited during an attempt. Failed/Cancel/loss paths never write.
            if (!saved_settings::save_wifi(ssid.c_str(), password.c_str()))
                Serial.println("WiFi NVS save failed; connection remains usable this boot.");
            connecting = false;
            configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
            page = 3; // Useful weather first while finance arrives sequentially.
            dashboard_touch_armed = false;
            draw_dashboard();
        } else if (now - connect_started >= CONNECT_TIMEOUT) {
            WiFi.disconnect(false, false);
            show_gate("Check WiFi / password. Retry.");
        } else if (now - last_animation >= 90) {
            last_animation = now;
            tft.fillRect(137, 146, 46, 36, WIFI_BG);
            for (int i = 0; i < 8; ++i) {
                float angle = i * PI / 4;
                tft.fillCircle(160 + cosf(angle) * 13, 164 + sinf(angle) * 13, 3,
                    i == spinner_phase ? TFT_WHITE : (i == (spinner_phase + 7) % 8 ? WIFI_MUTED : WIFI_KEY));
            }
            spinner_phase = (spinner_phase + 1) % 8;
        }
    }
}
} // namespace

void setup() {
    Serial.begin(115200);
    for (uint8_t pin : {uint8_t(4), uint8_t(16), uint8_t(17)}) {
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
    }
    tft.init();
    tft.setRotation(3);
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);
    touch_spi.begin(25, 39, 32, TOUCH_CS);
    touch.begin(touch_spi);
    touch.setRotation(1); // Canonical raw coordinates: never invalidate saved fits for display rotation.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    Serial.println("CYD LVGL Desk / c=forget calibration, w=forget WiFi, t=touch diagnostics, h=heap, n=next. Credentials stored in plaintext NVS, never logged.");
    Serial.println("Touch pins provisional; begin() is not chip detection. Pressure polling, fixed rotation 1.");
    if (!live_data::begin()) Serial.println("API worker unavailable; dashboard will show an error.");
    restore_startup();
}

void loop() {
    while (Serial.available()) {
        char command = Serial.read();
        if (command == 'c') force_recalibration();
        else if (command == 'w') forget_saved_wifi();
        else if (command == 'n') next_page();
        else if (command == 'h') {
            const auto stats = live_data::diagnostics(); // Small metadata only, no history arrays.
            Serial.printf("Heap free=%u min=%u largest8bit=%u loop-stack-min-free=%u API-stack-min-free=%u bytes (0 API=not measured yet)\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                unsigned(uxTaskGetStackHighWaterMark(nullptr)), unsigned(stats.worker_stack_free_bytes));
            const auto weather = dashboard::weather_stats();
            Serial.printf("Weather target=30 scheduled=%u.%u presented-roi=%u.%u fps window=%ums skipped=%u mixed=%u active=%u (SPI completion, 0ms=warming)\n",
                unsigned(weather.scheduled_fps_x10 / 10), unsigned(weather.scheduled_fps_x10 % 10),
                unsigned(weather.presented_roi_fps_x10 / 10), unsigned(weather.presented_roi_fps_x10 % 10),
                unsigned(weather.window_ms), unsigned(weather.skipped_slots), unsigned(weather.mixed_refreshes), unsigned(weather.active));
        }
        else if (command == 't') {
            touch_diagnostics = !touch_diagnostics;
            Serial.printf("Touch diagnostics %s (coordinates only).\n", touch_diagnostics ? "ON" : "OFF");
        }
    }
    uint32_t now = millis();
    update_network(now);
    poll_touch(now);
    if (screen == Screen::DASHBOARD) dashboard::tick(now);
}
