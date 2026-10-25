#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
namespace dashboard {
bool begin(TFT_eSPI &display);
void show(uint8_t page = 3);
void hide();
void next();
void touch(bool pressed, int x, int y);
void tick(uint32_t now);
struct WeatherStats {
    uint32_t window_ms = 0;
    uint32_t scheduled_fps_x10 = 0;
    uint32_t presented_roi_fps_x10 = 0;
    uint32_t skipped_slots = 0;
    uint32_t mixed_refreshes = 0;
    bool active = false;
};
// Last completed >=2s visible-weather window. Presented counts last SPI flushes, not draw calls.
WeatherStats weather_stats();
}
