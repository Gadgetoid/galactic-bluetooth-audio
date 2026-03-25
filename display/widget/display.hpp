#pragma once

#include "hardware/pio.h"

class Display {
  public:
    static const int WIDTH  = 53;
    static const int HEIGHT = 7;

    // These are actually the encoder pins, we need better ways to handle input/audio per device
    static const int SWITCH_A = 21;
    static const int SWITCH_B = 22;

    // pin assignments
    static const uint8_t COLUMN_CLOCK           = 12;
    static const uint8_t COLUMN_DATA            = 13;
    static const uint8_t COLUMN_LATCH           = 14;
    static const uint8_t COLUMN_BLANK           = 15;

    static const uint8_t ROW_DATA               = 16;
    static const uint8_t ROW_DATA_CLOCK         = 17;

  private:
    static const uint32_t VBLANK_ROWS           = 0;
    static const uint32_t ROW_COUNT = HEIGHT + VBLANK_ROWS;
    static const uint32_t COL_COUNT = WIDTH;
    static const uint32_t BCD_FRAME_COUNT = 12;
    static const uint32_t BCD_FRAME_BYTES = 60;   // 2 + 53 + 1 + 4     # Align to 4 byte boundary
    static const uint32_t ROW_BYTES = BCD_FRAME_COUNT * BCD_FRAME_BYTES;

  public:
    static const uint32_t BITSTREAM_LENGTH = (ROW_COUNT * ROW_BYTES);

  private:
    static PIO bitstream_pio;
    static uint bitstream_sm;
    static uint bitstream_sm_offset;

    uint16_t brightness = 256;

    static Display* widget;


  public:
    ~Display();

    void init();
    static inline void pio_program_init(PIO pio, uint sm, uint offset);

    void clear();

    void update();

    void set_brightness(float value);
    float get_brightness();
    void adjust_brightness(float delta);

    void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);

  private:
    void dma_safe_abort(uint channel);
};
