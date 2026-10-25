#include "dashboard.h"
#include "dashboard_gesture.h"
#include "weather_cadence.h"
#include "live_data.h"
#include "station_logos.h"
#include <lvgl.h>
#include <math.h>
#include <time.h>

namespace dashboard {
namespace {
constexpr uint32_t BG = 0x080D19, PANEL = 0x141F32, MUTED = 0x8C9DB5;
constexpr uint32_t CYAN = 0x38E5F5, PINK = 0xFA4DA5, GOLD = 0xF7C859, RED = 0xFF4949;
TFT_eSPI *tft = nullptr;
lv_color_t pixels[320 * 8]; // Single 8-row RGB565 partial buffer: 5120 bytes, no full-screen sprite.
lv_disp_draw_buf_t draw_buffer;
lv_disp_drv_t display_driver;
lv_indev_drv_t input_driver;
lv_disp_t *display = nullptr;
lv_obj_t *root = nullptr;
bool active = false, input_pressed = false;
lv_point_t input_point{};
DashboardGesture gesture;
// UI thread owns this persistent snapshot; no full Snapshot temporaries on loopTask's stack.
live_data::Snapshot data;
uint8_t page = 3, stock = 0, station = 0;
uint32_t last_tick = 0, last_refresh = 0, last_age_refresh = 0;
int last_second = -2;
constexpr uint32_t WEATHER_FPS_WINDOW = 2000; // Target 30 fps, measured separately at SPI completion.
constexpr lv_area_t WEATHER_ART_AREA = {196, 48, 302, 131};
// LVGL 8.3's public invalidation adds five pixels per side for transform rounding.
constexpr lv_area_t WEATHER_DIRTY_AREA = {191, 43, 307, 136};
WeatherCadence weather_cadence;
WeatherStats weather_report;
bool weather_running = false, animation_pending = false;
bool refresh_started = false, refresh_roi_only = true;
uint32_t weather_window_started = 0, scheduled_frames = 0, presented_frames = 0;
uint32_t skipped_slots = 0, mixed_refreshes = 0;
uint32_t weather_elapsed_ms = 0;
struct WeatherPose {
    float seconds, cloud_x, cloud_y, back_x, back_y, sun_angle, pulse;
    float squash, lobe_left, lobe_centre, lobe_right;
    uint8_t flash;
} weather_pose{};
lv_draw_ctx_t *ctx = nullptr;
const char *const TICKERS[] = {"SET", "PTT", "PTG", "AOT", "ADVANC", "CPALL"};
const char *const STATIONS[] = {"PTT", "PT / PTG", "Bangchak"};

bool within(const lv_area_t &area, const lv_area_t &bounds) {
    return area.x1 >= bounds.x1 && area.y1 >= bounds.y1 && area.x2 <= bounds.x2 && area.y2 <= bounds.y2;
}
void set_weather_running(bool enabled, uint32_t now) {
    if (weather_running == enabled) return;
    weather_running = enabled;
    weather_cadence.reset(); animation_pending = false;
    if (enabled) {
        weather_report = {}; weather_report.active = true;
        weather_window_started = now;
        scheduled_frames = presented_frames = skipped_slots = mixed_refreshes = 0;
    } else weather_report.active = false;
}
void close_weather_window(uint32_t now) {
    if (!weather_running) return;
    const uint32_t elapsed = now - weather_window_started;
    if (elapsed < WEATHER_FPS_WINDOW) return;
    weather_report.window_ms = elapsed;
    weather_report.scheduled_fps_x10 = uint32_t(uint64_t(scheduled_frames) * 10000U / elapsed);
    weather_report.presented_roi_fps_x10 = uint32_t(uint64_t(presented_frames) * 10000U / elapsed);
    weather_report.skipped_slots = skipped_slots; weather_report.mixed_refreshes = mixed_refreshes;
    weather_window_started = now;
    scheduled_frames = presented_frames = skipped_slots = mixed_refreshes = 0;
}
void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    // Only tick() runs LVGL, only while dashboard owns TFT. Setup never races this callback.
    const bool last_flush = lv_disp_flush_is_last(driver);
    if (!refresh_started) { refresh_started = true; refresh_roi_only = true; }
    refresh_roi_only = refresh_roi_only && within(*area, WEATHER_DIRTY_AREA);
    if (active) {
        tft->startWrite();
        tft->setAddrWindow(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
        tft->pushColors(reinterpret_cast<uint16_t *>(colors),
            (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), true);
        tft->endWrite();
    }
    if (last_flush) {
        // Single synchronous TFT transfer owner: last chunk has physically left pushColors/endWrite.
        // Exclude full/mixed data/page refreshes from the animation-only presented FPS.
        if (weather_running && active && page == 3) {
            if (animation_pending && refresh_roi_only) ++presented_frames;
            else ++mixed_refreshes;
        }
        animation_pending = false; refresh_started = false;
    }
    lv_disp_flush_ready(driver);
}
void read_input(lv_indev_drv_t *, lv_indev_data_t *out) {
    out->state = active && input_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    out->point = input_point;
}
void box(int x, int y, int w, int h, uint32_t fill, int radius = 10,
         uint32_t border = 0, int border_width = 0, lv_opa_t opacity = LV_OPA_COVER) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(fill); d.radius = radius; d.bg_opa = opacity;
    d.border_color = lv_color_hex(border); d.border_width = border_width;
    lv_area_t a = {lv_coord_t(x), lv_coord_t(y), lv_coord_t(x + w - 1), lv_coord_t(y + h - 1)};
    lv_draw_rect(ctx, &d, &a);
}
void label(const char *text, int x, int y, int width = 300, uint32_t color = 0xFFFFFF,
           bool large = false, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
    d.font = large ? &lv_font_montserrat_26 : &lv_font_montserrat_18;
    d.color = lv_color_hex(color); d.align = align;
    lv_area_t a = {lv_coord_t(x), lv_coord_t(y), lv_coord_t(x + width - 1), lv_coord_t(y + (large ? 30 : 22) - 1)};
    lv_draw_label(ctx, &d, &a, text, nullptr);
}
void line(int x1, int y1, int x2, int y2, uint32_t color, int width = 1, lv_opa_t opacity = LV_OPA_COVER) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(color); d.width = width; d.opa = opacity; d.round_start = d.round_end = 1;
    lv_point_t a = {lv_coord_t(x1), lv_coord_t(y1)}, b = {lv_coord_t(x2), lv_coord_t(y2)};
    lv_draw_line(ctx, &d, &a, &b);
}
void polygon(const lv_point_t *points, uint8_t count, uint32_t color, lv_opa_t opacity = LV_OPA_COVER) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color); d.bg_opa = opacity;
    lv_draw_polygon(ctx, &d, points, count);
}
void station_logo(uint8_t selected) {
    if (selected >= 3) return;
    // Pre-rasterized, antialiased RGB565 in flash; no SVG decoder or image allocation here.
    lv_draw_img_dsc_t d; lv_draw_img_dsc_init(&d);
    const lv_area_t area = {22, 91, 22 + station_logos::WIDTH - 1, 91 + station_logos::HEIGHT - 1};
    lv_draw_img(ctx, &d, &area, &station_logos::images[selected]);
}
void source_stamp(const live_data::Reading &r, char *text, size_t size) {
    if (r.source_epoch) {
        tm local{}; localtime_r(&r.source_epoch, &local);
        strftime(text, size, "%d/%m %H:%M BKK", &local);
    } else snprintf(text, size, "%s", r.source_time);
}
void status(live_data::Source source, int y) {
    const auto &r = data.readings[source]; char text[48];
    uint32_t color = MUTED;
    if (!data.worker_ok) { strcpy(text, "Worker unavailable"); color = RED; }
    else if (r.failed) {
        if (r.valid) {
            char stamp[32]; source_stamp(r, stamp, sizeof(stamp));
            snprintf(text, sizeof(text), "CACHED %s", stamp);
        } else snprintf(text, sizeof(text), "UNAVAILABLE / error %d", r.error);
        color = RED;
    }
    else if (!r.valid) strcpy(text, time(nullptr) < 1704067200 ? "Waiting for NTP" : "Loading data...");
    else if (live_data::stale(r, source, millis())) {
        char stamp[32]; source_stamp(r, stamp, sizeof(stamp));
        snprintf(text, sizeof(text), "STALE %s", stamp); color = RED;
    }
    else source_stamp(r, text, sizeof(text));
    label(text, 12, y, 296, color);
}
void value(float number, bool valid, char *out, size_t size, int decimals = 2) {
    if (valid) snprintf(out, size, "%.*f", decimals, number);
    else snprintf(out, size, "--");
}
void history(const live_data::Reading &r, int y, int height, uint32_t color) {
    box(10, y, 300, height, PANEL, 8);
    if (!r.history_count) { label("No history yet", 22, y + 12, 276, MUTED); return; }
    const int left = 22, right = 298, top = y + 8, bottom = y + height - 26;
    float low = r.history[0].price, high = low;
    for (int i = 1; i < r.history_count; ++i) {
        low = fminf(low, r.history[i].price); high = fmaxf(high, r.history[i].price);
    }
    const float span = high > low ? high - low : fmaxf(1, low * 0.001f);
    if (high == low) { low -= span / 2; high += span / 2; }
    const time_t first = r.history[0].epoch, last = r.history[r.history_count - 1].epoch;
    line(left, bottom, right, bottom, 0x32425A);
    line(left, (top + bottom) / 2, right, (top + bottom) / 2, 0x243248);
    if (r.history_count == 1)
        label("1 real sample", 84, y + 9, 204, MUTED);
    else if (r.history_observed)
        label("Observed / since boot", 50, y + 2, 240, MUTED);
    int px = 0, py = 0;
    for (int i = 0; i < r.history_count; ++i) {
        int x = last > first ? left + int(double(r.history[i].epoch - first) * (right - left) / (last - first)) : left;
        int cy = bottom - int((r.history[i].price - low) * (bottom - top) / (high - low));
        if (i) line(px, py, x, cy, color, 2);
        box(x - 2, cy - 2, 5, 5, color, 2);
        px = x; py = cy;
    }
    char dates[24]; tm local{}; localtime_r(&first, &local);
    strftime(dates, sizeof(dates), page == 1 || r.history_observed ? "%d/%m %H:%M" : "%d/%m", &local);
    label(dates, left, y + height - 23, 136, MUTED);
    localtime_r(&last, &local);
    strftime(dates, sizeof(dates), page == 1 || r.history_observed ? "%d/%m %H:%M" : "%d/%m", &local);
    label(dates, right - 136, y + height - 23, 136, MUTED, false, LV_TEXT_ALIGN_RIGHT);
}
// Original vector-like seven-segment artwork, shared by clock and pump prices.
void digital(const char *text, int x, int y, int scale, uint32_t color) {
    static const uint8_t masks[] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
    const int rects[7][4] = {{2,0,8,2},{10,2,2,8},{10,12,2,8},{2,20,8,2},{0,12,2,8},{0,2,2,8},{2,10,8,2}};
    for (const char *p = text; *p; ++p) {
        if (*p == '.' || *p == ':') {
            box(x, y + 18 * scale, 2 * scale, 2 * scale, color, scale);
            if (*p == ':') box(x, y + 6 * scale, 2 * scale, 2 * scale, color, scale);
            x += 4 * scale; continue;
        }
        uint8_t mask = *p >= '0' && *p <= '9' ? masks[*p - '0'] : 0x40;
        for (int i = 0; i < 7; ++i) {
            const int *r = rects[i];
            box(x + r[0] * scale, y + r[1] * scale, r[2] * scale, r[3] * scale,
                mask & (1 << i) ? color : 0x302334, scale);
        }
        x += 15 * scale;
    }
}
void stocks() {
    label("THAI MARKET", 12, 7, 296, CYAN, true);
    for (int i = 0; i < 6; ++i) {
        const int x = 10 + (i % 3) * 101, y = 40 + (i / 3) * 31;
        box(x, y, 98, 28, i == stock ? 0x22535D : PANEL, 8, i == stock ? CYAN : PANEL, 1);
        label(TICKERS[i], x, y + 3, 98, i == stock ? CYAN : MUTED, false, LV_TEXT_ALIGN_CENTER);
    }
    auto source = static_cast<live_data::Source>(live_data::SET + stock);
    const auto &r = data.readings[source]; char text[40];
    value(r.values[0], r.valid, text, sizeof(text));
    label(text, 12, 105, 170, 0xFFFFFF, true);
    label(stock ? "THB" : "points", 204, 112, 100, CYAN, false, LV_TEXT_ALIGN_RIGHT);
    status(source, 138);
    history(r, 164, 74, CYAN);
}
void bullion_quote(float price, bool valid, int y, uint32_t color) {
    char text[24]; value(price, valid, text, sizeof(text), 0);
    if (strlen(text) <= 6) digital(text, 18, y, 2, color);
    else label(text, 18, y + 6, 184, color, true); // Long prices still fit without dropping digits.
}
void gold() {
    label("THAI GOLD 96.5%", 12, 7, 296, GOLD, true);
    const auto &r = data.readings[live_data::THAI_GOLD];
    box(10, 40, 194, 75, PANEL, 10);
    box(10, 120, 194, 75, PANEL, 10);
    label("BUY", 20, 44, 164, GOLD);
    bullion_quote(r.values[1], r.valid, 68, GOLD);
    label("SELL", 20, 124, 164, CYAN);
    bullion_quote(r.values[0], r.valid, 148, 0xFFFFFF);
    // Original stacked bullion artwork occupies the reclaimed graph space.
    box(228, 75, 76, 32, 0xAD7824, 5);
    box(219, 69, 76, 32, 0xE6AD3D, 5, 0xFFE5A0, 1);
    box(215, 49, 78, 36, GOLD, 5, 0xFFF0B8, 2);
    line(222, 56, 284, 56, 0xFFF4C8, 2);
    label("96.5", 222, 59, 64, 0x684715, false, LV_TEXT_ALIGN_CENTER);
    label("THB", 212, 113, 96, MUTED, false, LV_TEXT_ALIGN_CENTER);
    label("/ baht wt.", 209, 135, 99, MUTED, false, LV_TEXT_ALIGN_CENTER);
    label("UPDATED", 210, 159, 98, GOLD, false, LV_TEXT_ALIGN_CENTER);
    char clock[6] = "--:--";
    if (r.valid && strlen(r.source_time) >= 16 && r.source_time[13] == ':')
        memcpy(clock, r.source_time + 11, 5);
    label(clock, 211, 179, 96, 0xFFFFFF, true, LV_TEXT_ALIGN_CENTER);
    box(10, 211, 300, 27, PANEL, 7);
    status(live_data::THAI_GOLD, 213); // Full source date/time and cached/error remain visible.
}
void oil() {
    label("FUEL / THB PER L", 12, 7, 296, CYAN, true);
    const uint32_t colors[] = {0x52BAF8, 0x72D348, 0xB8DF44};
    for (int i = 0; i < 3; ++i) {
        box(10 + 101 * i, 40, 98, 29, station == i ? 0x263C37 : PANEL, 8, colors[i], station == i ? 2 : 0);
        label(STATIONS[i], 10 + 101 * i, 45, 98, colors[i], false, LV_TEXT_ALIGN_CENTER);
    }
    box(12, 82, 80, 116, PANEL, 10, colors[station], 2);
    station_logo(station);
    box(25, 160, 51, 26, colors[station], 5);
    for (int y = 167; y <= 179; y += 6) line(34, y, 67, y, 0x20372C, 2);
    // Keep the hose outside the logo so the small bangchak lettering stays unobstructed.
    line(85, 126, 90, 137, MUTED, 3); line(90, 137, 90, 185, MUTED, 3);
    line(90, 185, 78, 185, MUTED, 3);
    const auto &r = data.readings[live_data::THAI_OIL];
    const char *products[] = {"Gasohol 95", "Gasohol 91", "E20", "Diesel B7"};
    for (int i = 0; i < 4; ++i) {
        const int y = 77 + i * 33, index = station * 4 + i;
        box(103, y, 207, 31, PANEL, 6);
        label(products[i], 109, y + 5, 109, MUTED);
        char price[16]; value(r.values[index], r.valid && (r.available & (1U << index)), price, sizeof(price));
        digital(price, 229, y + 4, 1, RED);
    }
    status(live_data::THAI_OIL, 211);
}
enum class WeatherScene : uint8_t { UNKNOWN, CLEAR, MAINLY_CLEAR, PARTLY_CLOUDY, OVERCAST, FOG, DRIZZLE, RAIN, SNOW, THUNDER };
WeatherScene weather_scene(int code) {
    switch (code) {
        case 0: return WeatherScene::CLEAR;
        case 1: return WeatherScene::MAINLY_CLEAR;
        case 2: return WeatherScene::PARTLY_CLOUDY;
        case 3: return WeatherScene::OVERCAST;
        case 45: case 48: return WeatherScene::FOG;
        case 51: case 53: case 55: case 56: case 57: return WeatherScene::DRIZZLE;
        case 61: case 63: case 65: case 66: case 67: case 80: case 81: case 82: return WeatherScene::RAIN;
        case 71: case 73: case 75: case 77: case 85: case 86: return WeatherScene::SNOW;
        case 95: case 96: case 99: return WeatherScene::THUNDER;
        default: return WeatherScene::UNKNOWN;
    }
}
const char *condition(int code) {
    if (code == 56 || code == 57) return "Freezing drizzle";
    if (code == 66 || code == 67) return "Freezing rain";
    if (code == 80 || code == 81 || code == 82) return "Rain showers";
    if (code == 85 || code == 86) return "Snow showers";
    if (code == 96 || code == 99) return "Hail / thunder";
    switch (weather_scene(code)) {
        case WeatherScene::CLEAR: return "Clear sky";
        case WeatherScene::MAINLY_CLEAR: return "Mainly clear";
        case WeatherScene::PARTLY_CLOUDY: return "Partly cloudy";
        case WeatherScene::OVERCAST: return "Overcast";
        case WeatherScene::FOG: return "Fog";
        case WeatherScene::DRIZZLE: return "Drizzle";
        case WeatherScene::RAIN: return "Rain";
        case WeatherScene::SNOW: return "Snow";
        case WeatherScene::THUNDER: return "Thunderstorm";
        default: return "Unknown WMO";
    }
}
uint8_t flash_pulse(uint32_t phase, uint32_t start, uint32_t rise, uint32_t fall) {
    const uint32_t elapsed = phase - start;
    if (elapsed >= rise + fall) return 0;
    return elapsed < rise ? uint8_t(elapsed * 255 / rise) : uint8_t((rise + fall - elapsed) * 255 / fall);
}
void sample_weather(uint32_t elapsed_ms) {
    // Bounded periodic time, sampled once per frame. All motion uses elapsed time, not frame count.
    const float t = (elapsed_ms % 120000U) * 0.001f;
    constexpr float TAU = 6.283185307f;
    weather_pose.seconds = t;
    weather_pose.cloud_x = 8.0f * sinf(TAU * t / 8.0f);
    weather_pose.cloud_y = 2.6f * sinf(TAU * t / 4.0f + 1.1f);
    weather_pose.back_x = 6.5f * sinf(TAU * t / 12.0f + 1.2f);
    weather_pose.back_y = 2.3f * sinf(TAU * t / 10.0f + 0.5f);
    weather_pose.squash = sinf(TAU * t / 6.0f + 0.6f);
    weather_pose.lobe_left = sinf(TAU * t / 3.0f);
    weather_pose.lobe_centre = sinf(TAU * t / 4.0f + 1.2f);
    weather_pose.lobe_right = sinf(TAU * t / 5.0f + 2.4f);
    weather_pose.sun_angle = TAU * t / 12.0f;
    weather_pose.pulse = sinf(TAU * t / 3.0f);
    const uint32_t storm_phase = elapsed_ms % 6000U;
    const uint8_t first = flash_pulse(storm_phase, 1800, 28, 132);
    const uint8_t second = flash_pulse(storm_phase, 2200, 18, 62);
    weather_pose.flash = first > second ? first : second;
}
uint32_t tint(uint32_t from, uint32_t to, uint8_t amount) {
    const uint32_t inverse = 255 - amount;
    const uint32_t r = (((from >> 16) & 255) * inverse + ((to >> 16) & 255) * amount) / 255;
    const uint32_t g = (((from >> 8) & 255) * inverse + ((to >> 8) & 255) * amount) / 255;
    const uint32_t b = ((from & 255) * inverse + (to & 255) * amount) / 255;
    return (r << 16) | (g << 8) | b;
}
void sun(int x, int y) {
    const float angle = weather_pose.sun_angle;
    const int reach = 24 + int(lroundf(1.8f * weather_pose.pulse));
    box(x - 18, y - 18, 37, 37, GOLD, 19, 0, 0, uint8_t(22 + 10 * weather_pose.pulse));
    for (int ray = 0; ray < 8; ++ray) {
        const float a = angle + ray * 0.785398163f;
        line(x + int(lroundf(cosf(a) * 19)), y + int(lroundf(sinf(a) * 19)),
             x + int(lroundf(cosf(a) * reach)), y + int(lroundf(sinf(a) * reach)), GOLD, 2);
    }
    box(x - 15, y - 15, 31, 31, GOLD, 16);
    box(x - 10, y - 11, 15, 9, 0xFFE8A0, 5);
}
void cloud_lobe(float x, float y, float width, float height, uint32_t color) {
    const int w = int(lroundf(width)), h = int(lroundf(height));
    box(int(lroundf(x)), int(lroundf(y)), w, h, color, (w < h ? w : h) / 2);
}
void cloud(int x, int y, uint32_t color, bool back = false) {
    // Morph around the centre: independently breathing lobes plus opposing squash/stretch.
    // No bitmap rebuilding or allocation; every partial draw uses the same sampled pose.
    const float a = back ? weather_pose.lobe_right : weather_pose.lobe_left;
    const float b = back ? weather_pose.lobe_left : weather_pose.lobe_centre;
    const float c = back ? weather_pose.lobe_centre : weather_pose.lobe_right;
    const float sx = 1.0f + 0.065f * (back ? -weather_pose.squash : weather_pose.squash);
    const float sy = 1.0f - 0.085f * (back ? -weather_pose.squash : weather_pose.squash);
    const float left = 32 + 3.0f * a, middle = 40 + 3.6f * b, right = 28 + 2.8f * c;
    cloud_lobe(x + 38 + (3 + a - 38) * sx, y + 22 + (8 - a - 22) * sy, left * sx, left * sy, color);
    cloud_lobe(x + 38 + (20 + b - 38) * sx, y + 22 + (-1.4f * b - 22) * sy, middle * sx, middle * sy, color);
    cloud_lobe(x + 38 + (48 - c - 38) * sx, y + 22 + (13 + c - 22) * sy, right * sx, right * sy, color);
    cloud_lobe(x + 38 - 30 * sx, y + 22, 65 * sx, 20 * sy, color);
}
void weather_art(WeatherScene scene, int code) {
    lv_area_t clipped;
    if (!_lv_area_intersect(&clipped, ctx->clip_area, &WEATHER_ART_AREA)) return;
    const lv_area_t *original_clip = ctx->clip_area;
    ctx->clip_area = &clipped; // Rain, rays and cloud drift can never paint over numbers/text.
    const int drift = int(lroundf(weather_pose.cloud_x));
    const int bob = int(lroundf(weather_pose.cloud_y));
    if (scene == WeatherScene::UNKNOWN) {
        label("--", 221, 78, 60, MUTED, true, LV_TEXT_ALIGN_CENTER);
    } else if (scene == WeatherScene::CLEAR) {
        sun(249, 88);
    } else {
        if (scene == WeatherScene::MAINLY_CLEAR || scene == WeatherScene::PARTLY_CLOUDY) sun(267, 74);
        const bool dark = scene == WeatherScene::RAIN || scene == WeatherScene::THUNDER;
        const uint8_t flash = scene == WeatherScene::THUNDER ? weather_pose.flash : 0;
        if (scene != WeatherScene::MAINLY_CLEAR)
            cloud(219 + int(lroundf(weather_pose.back_x)), 64 + int(lroundf(weather_pose.back_y)),
                  dark ? tint(0x445C7D, 0xD6E9FD, flash) : 0x8FA6C4, true);
        cloud(202 + drift, 75 + bob, dark ? tint(0x859BB7, 0xF4FAFF, flash) : 0xD9E6F3);
        if (scene == WeatherScene::FOG) {
            for (int i = 0; i < 3; ++i) line(204 + drift, 109 + i * 9, 291 + drift, 109 + i * 9, MUTED, 2);
        } else if (scene == WeatherScene::DRIZZLE || scene == WeatherScene::RAIN || scene == WeatherScene::THUNDER) {
            const int count = scene == WeatherScene::DRIZZLE ? 4 : 7;
            for (int i = 0; i < count; ++i) {
                const float period = 0.8f + (i % 3) * 0.2f;
                const float phase = fmodf(weather_pose.seconds / period + i * 0.137f, 1.0f);
                const float x = 214.0f + i * 11 + weather_pose.cloud_x - 6 * phase;
                const float y = 106.0f + phase * 33;
                const float length = scene == WeatherScene::DRIZZLE ? 4.0f : 7.0f + (i % 2) * 2;
                // Three short opacity-graded trail pieces, with independently phased drops.
                for (int trail = 0; trail < 3; ++trail) {
                    const float offset = length * trail / 3;
                    line(int(lroundf(x + offset * 0.35f)), int(lroundf(y - offset)),
                         int(lroundf(x + (offset + length / 3) * 0.35f)), int(lroundf(y - offset - length / 3)),
                         CYAN, 2, uint8_t(235 - trail * 70));
                }
            }
            if (flash > 10) {
                static const lv_point_t bolt[] = {{252, 101}, {241, 116}, {252, 113}};
                polygon(bolt, 3, GOLD, flash);
                line(252, 113, 245, 129, GOLD, 3, flash);
            }
            if (code == 96 || code == 99) {
                for (int i = 0; i < 3; ++i) {
                    const float phase = fmodf(weather_pose.seconds / (0.6f + i * 0.2f) + i * 0.31f, 1.0f);
                    const int y = int(lroundf(107 + phase * 31));
                    box(221 + i * 23, y, 4, 4, 0xF4FCFF, 2);
                }
            }
        } else if (scene == WeatherScene::SNOW) {
            for (int i = 0; i < 5; ++i) {
                const float phase = fmodf(weather_pose.seconds / (i % 2 ? 2.4f : 3.0f) + i * 0.23f, 1.0f);
                const int x = int(lroundf(215 + i * 17 + weather_pose.cloud_x + 2 * sinf(phase * 6.283185307f)));
                const int y = int(lroundf(107 + phase * 32));
                line(x - 2, y, x + 2, y, 0xF4FCFF, 1);
                line(x, y - 2, x, y + 2, 0xF4FCFF, 1);
            }
        }
    }
    ctx->clip_area = original_clip;
}
void weather() {
    label("BANGKOK", 12, 7, 296, CYAN, true);
    const auto &r = data.readings[live_data::WEATHER]; char text[40];
    box(12, 44, 296, 92, PANEL, 14);
    const int code = r.valid ? int(r.values[3]) : -1;
    weather_art(weather_scene(code), code);
    if (r.valid) snprintf(text, sizeof(text), "%.1f C", r.values[0]); else strcpy(text, "-- C");
    label(text, 26, 57, 164, 0xFFFFFF, true);
    label(r.valid ? condition(code) : "--", 26, 103, 164, CYAN);
    if (r.valid) snprintf(text, sizeof(text), "Feels %.1f C", r.values[2]); else strcpy(text, "Feels -- C");
    label(text, 12, 144, 296, 0xFFFFFF, true);
    box(12, 179, 142, 30, PANEL, 8); box(163, 179, 145, 30, PANEL, 8);
    if (r.valid) snprintf(text, sizeof(text), "RH %.0f%%", r.values[1]); else strcpy(text, "RH --%");
    label(text, 22, 184, 125, CYAN);
    if (r.valid) snprintf(text, sizeof(text), "%.1f km/h", r.values[4]); else strcpy(text, "-- km/h");
    label(text, 173, 184, 130, CYAN);
    status(live_data::WEATHER, 215);
}
void clock() {
    label("BANGKOK / UTC+7", 12, 7, 296, PINK, true);
    for (int y = 43; y < 191; y += 7) line(12, y, 307, y, 0x19243C);
    box(10, 43, 300, 147, BG, 14, PINK, 2);
    // Scan lines confined to the clock face, with neon segments over them.
    for (int y = 49; y < 185; y += 7) line(18, y, 300, y, 0x142137);
    time_t now = time(nullptr); tm local{}; char text[48];
    const bool valid = now >= 1704067200;
    if (valid) { localtime_r(&now, &local); strftime(text, sizeof(text), "%H:%M", &local); }
    else strcpy(text, "--:--");
    digital(text, 30, 58, 4, CYAN);
    if (valid) strftime(text, sizeof(text), "%a %d %b %Y", &local); else strcpy(text, "Waiting for NTP");
    label(text, 18, 159, 284, MUTED, false, LV_TEXT_ALIGN_CENTER);
    if (valid) snprintf(text, sizeof(text), "%02d seconds", local.tm_sec); else strcpy(text, "-- seconds");
    label(text, 12, 204, 296, PINK, true, LV_TEXT_ALIGN_CENTER);
}
void draw(lv_event_t *event) {
    ctx = lv_event_get_draw_ctx(event);
    box(0, 0, 320, 240, BG, 0);
    const lv_area_t *clip = ctx->clip_area;
    if (page == 3 && clip->x1 >= WEATHER_DIRTY_AREA.x1 && clip->x2 <= WEATHER_DIRTY_AREA.x2 &&
        clip->y1 >= WEATHER_DIRTY_AREA.y1 && clip->y2 <= WEATHER_DIRTY_AREA.y2) {
        // This region is right of all weather text. Skip float/date formatting and glyph work
        // during animation-only partial flushes; full data/page redraws still use weather().
        box(12, 44, 296, 92, PANEL, 14);
        const auto &r = data.readings[live_data::WEATHER];
        const int code = r.valid ? int(r.values[3]) : -1;
        weather_art(weather_scene(code), code);
        return;
    }
    if (page == 0) stocks(); else if (page == 1) gold(); else if (page == 2) oil();
    else if (page == 3) weather(); else clock();
}
int selected_tab(int x, int y, bool oil_tab) {
    const int relative_x = x - 10, relative_y = y - 40;
    if (relative_x < 0 || relative_x >= 300 || relative_y < 0) return -1;
    if (relative_x % 101 >= 98) return -1;
    if (oil_tab) return relative_y < 29 ? relative_x / 101 : -1;
    if (relative_y >= 59 || relative_y % 31 >= 28) return -1;
    return (relative_y / 31) * 3 + relative_x / 101;
}
void change_page(int direction) {
    page = (page + 5 + direction) % 5;
    set_weather_running(false, millis());
    lv_obj_invalidate(root);
}
} // namespace
bool begin(TFT_eSPI &screen) {
    if (root) return true;
    tft = &screen; lv_init();
    lv_disp_draw_buf_init(&draw_buffer, pixels, nullptr, 320 * 8);
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = 320; display_driver.ver_res = 240;
    display_driver.flush_cb = flush; display_driver.draw_buf = &draw_buffer;
    display = lv_disp_drv_register(&display_driver);
    if (!display) return false;
    root = lv_disp_get_scr_act(display);
    lv_obj_remove_style_all(root);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root, draw, LV_EVENT_DRAW_MAIN, nullptr);
    lv_indev_drv_init(&input_driver); input_driver.type = LV_INDEV_TYPE_POINTER;
    input_driver.read_cb = read_input; input_driver.disp = display;
    lv_indev_drv_register(&input_driver);
    last_tick = millis();
    return true;
}
void show(uint8_t selected) {
    if (!root) return;
    page = selected % 5; active = true; gesture = {}; input_pressed = false;
    last_tick = millis(); set_weather_running(false, last_tick);
    sample_weather(weather_elapsed_ms);
    live_data::copy_snapshot(data); last_refresh = millis();
    last_second = -2; lv_obj_invalidate(root);
}
void hide() {
    active = false; input_pressed = false; gesture = {};
    set_weather_running(false, millis());
}
void next() { if (active) change_page(1); }
void touch(bool pressed, int x, int y) {
    if (!active) return;
    input_pressed = pressed; input_point = {lv_coord_t(x), lv_coord_t(y)};
    const auto action = gesture.update(pressed, x, y, millis());
    if (action == DashboardGesture::LEFT) change_page(1);
    else if (action == DashboardGesture::RIGHT) change_page(-1);
    else if (action == DashboardGesture::TAP) {
        if (page == 0 || page == 2) {
            const int start = selected_tab(gesture.start_x, gesture.start_y, page == 2);
            const int end = selected_tab(gesture.end_x, gesture.end_y, page == 2);
            if (start >= 0 && start == end) {
                if (page == 0) stock = end; else station = end;
                lv_obj_invalidate(root);
            }
        }
    }
}
void tick(uint32_t now) {
    if (!active || !root) return;
    const uint32_t elapsed = now - last_tick;
    lv_tick_inc(elapsed); last_tick = now;
    if (now - last_refresh >= 250) {
        last_refresh = now;
        // Main-thread-only snapshot and drawing; worker has no LVGL/TFT access.
        const uint32_t revision = data.revision;
        live_data::copy_snapshot(data);
        int second = int(time(nullptr));
        if (revision != data.revision || (page == 4 && second != last_second) || now - last_age_refresh >= 60000) {
            last_second = second; last_age_refresh = now; lv_obj_invalidate(root);
        }
    }
    const auto &weather = data.readings[live_data::WEATHER];
    set_weather_running(page == 3 && weather.valid && weather_scene(int(weather.values[3])) != WeatherScene::UNKNOWN, now);
    if (weather_running) {
        weather_elapsed_ms = (weather_elapsed_ms + elapsed % 120000U) % 120000U;
        const uint32_t slots = weather_cadence.advance(elapsed);
        if (slots) {
            ++scheduled_frames; skipped_slots += slots - 1;
            sample_weather(weather_elapsed_ms);
            animation_pending = true;
            lv_obj_invalidate_area(root, &WEATHER_ART_AREA);
            // Make THIS display timer due now, rather than rounding 33/34ms to its 20ms poll.
            // Pinned LVGL 8.3 accessor; no global refresh-rate change or recursive rendering.
            lv_timer_ready(_lv_disp_get_refr_timer(display));
        }
    }
    lv_timer_handler(); // Exactly one main-thread handler call; no timers/render callback re-entry.
    close_weather_window(millis());
}
WeatherStats weather_stats() { return weather_report; }
} // namespace dashboard
