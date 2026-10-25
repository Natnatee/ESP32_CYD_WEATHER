#pragma once
#include <stdint.h>

// Per-source circuit: a failed weather endpoint must not block gold/oil/stocks.
struct ApiRetry {
    uint32_t attempted = 0, wait = 0;
    uint8_t failures = 0;
    bool tried = false;
    bool ready(uint32_t now) const { return !tried || now - attempted >= wait; }
    void finish(uint32_t now, bool success, uint32_t normal_interval) {
        tried = true; attempted = now;
        if (success) { failures = 0; wait = normal_interval; return; }
        if (failures < 6) ++failures;
        wait = 30000U << (failures - 1); // 30s, 60s, then open circuit >=5min.
        if (failures >= 3 && wait < 300000U) wait = 300000U;
        if (wait > 900000U) wait = 900000U;
    }
};
