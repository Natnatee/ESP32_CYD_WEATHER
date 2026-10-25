#pragma once
#include <Arduino.h>
#include <time.h>

namespace live_data {
enum Source : uint8_t { WEATHER, THAI_GOLD, THAI_OIL, SET, PTT, PTG, AOT, ADVANC, CPALL, COUNT };
constexpr uint8_t HISTORY_CAPACITY = 32;
const char *const STOCK_SYMBOLS[] = {"^SET.BK", "PTT.BK", "PTG.BK", "AOT.BK", "ADVANC.BK", "CPALL.BK"};
const char *const OIL_STATIONS[] = {"ptt", "pt", "bcp"};
const char *const OIL_PRODUCTS[] = {"gasohol_95", "gasohol_91", "gasohol_e20", "diesel"};
struct Observation { time_t epoch; float price; };
struct Reading {
    float values[12]{};
    uint16_t available = 0; // Oil: station * 4 + product, independently available.
    Observation history[HISTORY_CAPACITY]{};
    uint8_t history_count = 0;
    bool history_observed = false; // SET/stock fallback when Yahoo supplies <2 daily points.
    bool valid = false;
    bool failed = false;
    int error = 0; // HTTP status, or negative transport/JSON/schema error.
    uint32_t received_ms = 0;
    time_t source_epoch = 0;
    char source_time[32]{}; // ASCII source-local date/time when epoch unavailable.
};
struct Snapshot {
    Reading readings[COUNT]{};
    uint32_t revision = 0;
    bool worker_ok = false;
    uint32_t worker_stack_free_bytes = 0; // ESP-IDF high-water mark, measured after fetch.
};
constexpr uint32_t WEATHER_INTERVAL = 600000;
constexpr uint32_t FINANCE_INTERVAL = 900000;
struct Diagnostics {
    uint32_t revision;
    bool worker_ok;
    uint32_t worker_stack_free_bytes;
};
bool begin();
// Copy directly into caller-owned storage (UI uses static RAM), never return a full Snapshot.
void copy_snapshot(Snapshot &destination);
Diagnostics diagnostics();
bool stale(const Reading &reading, Source source, uint32_t now);
// Strict comma-grouped numeric strings used by the Thai APIs; missing is never zero.
bool parse_price(const char *value, float &result);
void append_observation(Reading &reading, time_t epoch, float price);
} // namespace live_data
