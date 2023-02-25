#pragma once
#include <cstdint>

struct RGB {
    uint8_t r, g, b;

    constexpr RGB() : r(0), g(0), b(0) {}
    constexpr RGB(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}

    static RGB from_hsv(float h, float s, float v) {
        int i = h * 6.0f;
        float f = (h * 6.0f) - i;
        v *= 255.0f;
        uint8_t p = v * (1.0f - s);
        uint8_t q = v * (1.0f - (f * s));
        uint8_t t = v * (1.0f - ((1.0f - f) * s));

        switch (i % 6) {
        default:
        case 0: return RGB(v, t, p);
        case 1: return RGB(q, v, p);
        case 2: return RGB(p, v, t);
        case 3: return RGB(p, q, v);
        case 4: return RGB(t, p, v);
        case 5: return RGB(v, p, q);
        };
    }
};