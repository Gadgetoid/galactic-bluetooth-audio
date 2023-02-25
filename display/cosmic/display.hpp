#pragma once

#include "hardware/pio.h"
#include "displaybase.hpp"

class Display : public DisplayBase {
  public:
    static const int WIDTH  = 32;
    static const int HEIGHT = 32;

    // pin assignments
    static const uint8_t COLUMN_CLOCK           = 13;
    static const uint8_t COLUMN_DATA            = 14;
    static const uint8_t COLUMN_LATCH           = 15;
    static const uint8_t COLUMN_BLANK           = 16;

    static const uint8_t ROW_BIT_0              = 17;
    static const uint8_t ROW_BIT_1              = 18;
    static const uint8_t ROW_BIT_2              = 19;
    static const uint8_t ROW_BIT_3              = 20;
  
    static const uint8_t LIGHT_SENSOR           = 28;

    static const uint8_t MUTE                   = 22;

    static const uint8_t I2S_DATA               =  9;
    static const uint8_t I2S_BCLK               = 10;
    static const uint8_t I2S_LRCLK              = 11;

    static const uint8_t I2C_SDA                =  4;
    static const uint8_t I2C_SCL                =  5;

    static const uint8_t SWITCH_A               =  0;
    static const uint8_t SWITCH_B               =  1;
    static const uint8_t SWITCH_C               =  3;
    static const uint8_t SWITCH_D               =  6;

    static const uint8_t SWITCH_SLEEP           = 27;

    static const uint8_t SWITCH_VOLUME_UP       =  7;
    static const uint8_t SWITCH_VOLUME_DOWN     =  8;
    static const uint8_t SWITCH_BRIGHTNESS_UP   = 21;
    static const uint8_t SWITCH_BRIGHTNESS_DOWN = 26;

  private:
    static const uint32_t ROW_COUNT = 16;
    static const uint32_t BCD_FRAME_COUNT = 14;
    static const uint32_t BCD_FRAME_BYTES = 72;
    static const uint32_t ROW_BYTES = BCD_FRAME_COUNT * BCD_FRAME_BYTES;
    static const uint32_t BITSTREAM_LENGTH = (ROW_COUNT * ROW_BYTES);

  private:
    static PIO bitstream_pio;
    static uint bitstream_sm;
    static uint bitstream_sm_offset;;

    uint16_t brightness = 256;

    // must be aligned for 32bit dma transfer
    alignas(4) uint8_t bitstream[BITSTREAM_LENGTH] = {0};
    const uint32_t bitstream_addr = (uint32_t)bitstream;
    void dma_safe_abort(uint channel);

  public:
    ~Display();

    void init() override;
    void clear() override;
    void update() override;
    void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) override;

    const int get_width() override {return WIDTH;};
    const int get_height() override {return HEIGHT;};

    void set_brightness(float value);
    float get_brightness();
    void adjust_brightness(float delta);

    uint16_t light();
};