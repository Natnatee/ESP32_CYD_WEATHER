#pragma once
#include <stdint.h>
#include <stdlib.h>
// Horizontal drag owns the entire contact: it can never also select a tab.
struct DashboardGesture {
    bool down = false;
    int start_x = 0, start_y = 0, end_x = 0, end_y = 0, travel = 0;
    uint32_t started = 0;
    enum Action { NONE, TAP, LEFT, RIGHT };
    Action update(bool pressed, int x, int y, uint32_t now) {
        if (pressed) {
            if (!down) {
                down = true; start_x = end_x = x; start_y = end_y = y;
                travel = 0; started = now;
            }
            end_x = x; end_y = y;
            const int distance = abs(x - start_x) + abs(y - start_y);
            if (distance > travel) travel = distance;
            return NONE;
        }
        if (!down) return NONE;
        down = false;
        const int dx = end_x - start_x, dy = end_y - start_y;
        if (abs(dx) >= 55 && abs(dx) > abs(dy) * 2) return dx < 0 ? LEFT : RIGHT;
        if (travel <= 12 && now - started < 800) return TAP;
        return NONE;
    }
};
