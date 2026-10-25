#pragma once
#include <stdint.h>

// Rational 30 Hz deadlines on a millisecond clock: 34,33,33 ms, no cumulative drift.
// Late loops render at most one fresh frame; expired slots are reported, never replayed.
struct WeatherCadence {
    uint16_t credit = 0;
    uint32_t advance(uint32_t elapsed_ms) {
        const uint32_t scaled = (elapsed_ms % 1000U) * 30U + credit;
        const uint32_t slots = (elapsed_ms / 1000U) * 30U + scaled / 1000U;
        credit = scaled % 1000U;
        return slots;
    }
    void reset() { credit = 0; }
};
