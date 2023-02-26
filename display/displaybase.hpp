#pragma once
#include <cstdint>

class DisplayBase {
    public:
        virtual void init();
        virtual void clear();
        virtual void update();
        virtual void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
        virtual const int get_width() = 0;
        virtual const int get_height() = 0;
};