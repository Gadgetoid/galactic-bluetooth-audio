#include <math.h>

#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include "widget.pio.h"
#include "display.hpp"

// pixel data is stored as a stream of bits delivered in the
// order the PIO needs to manage the shift registers, row
// selects, delays, and latching/blanking
//
// the pins used are:
//
//  - 13: column clock (sideset)
//  - 14: column data  (out base)
//  - 15: column latch
//  - 16: column blank
//  - 17: row select bit 0
//  - 18: row select bit 1
//  - 19: row select bit 2
//  - 20: row select bit 3
//
// the framebuffer data is structured like this:
//
// for each row:
//   for each bcd frame:
//            0: 00111111                           // row pixel count (minus one)
//            1: xxxxrrrr                           // row select bits
//      2  - 27: xxxxxxxv, xxxxxxxv, xxxxxxxv, ...  // pixel data
//      66 - 67: xxxxxxxx, xxxxxxxx,                // dummy bytes to dword align
//      68 - 71: tttttttt, tttttttt, tttttttt       // bcd tick count (0-65536)
//
//  .. and back to the start

static uint32_t dma_channel;
static uint32_t dma_ctrl_channel;

/*
gamma = [int(round((n / 255.0) ** 2.2 * 4095)) for n in range(256)]

# Ensure no two gamma values are the same
for i in range(1, len(gamma)):
    if gamma[i] <= gamma[i - 1]:
        gamma[i] = gamma[i -1] + 1
*/
static const uint16_t __not_in_flash("gamma_table") GAMMA_12BIT[256] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 27, 29, 32, 34, 37, 40, 43, 46, 49, 52, 55, 59, 62, 66, 70, 73, 77, 82, 86, 90, 95, 99, 104, 109, 114, 119, 124, 129, 135, 140, 146, 152, 158, 164, 170, 176, 182, 189, 196, 202, 209, 216, 224, 231, 238, 246, 254, 261, 269, 277, 286, 294, 302, 311, 320, 328, 337, 347, 356, 365, 375, 384, 394, 404, 414, 424, 435, 445, 456, 467, 477, 488, 500, 511, 522, 534, 545, 557, 569, 581, 594, 606, 619, 631, 644, 657, 670, 683, 697, 710, 724, 738, 752, 766, 780, 794, 809, 823, 838, 853, 868, 884, 899, 914, 930, 946, 962, 978, 994, 1011, 1027, 1044, 1061, 1078, 1095, 1112, 1130, 1147, 1165, 1183, 1201, 1219, 1237, 1256, 1274, 1293, 1312, 1331, 1350, 1370, 1389, 1409, 1429, 1449, 1469, 1489, 1509, 1530, 1551, 1572, 1593, 1614, 1635, 1657, 1678, 1700, 1722, 1744, 1766, 1789, 1811, 1834, 1857, 1880, 1903, 1926, 1950, 1974, 1997, 2021, 2045, 2070, 2094, 2119, 2143, 2168, 2193, 2219, 2244, 2270, 2295, 2321, 2347, 2373, 2400, 2426, 2453, 2479, 2506, 2534, 2561, 2588, 2616, 2644, 2671, 2700, 2728, 2756, 2785, 2813, 2842, 2871, 2900, 2930, 2959, 2989, 3019, 3049, 3079, 3109, 3140, 3170, 3201, 3232, 3263, 3295, 3326, 3358, 3390, 3421, 3454, 3486, 3518, 3551, 3584, 3617, 3650, 3683, 3716, 3750, 3784, 3818, 3852, 3886, 3920, 3955, 3990, 4025, 4060, 4095};

  // must be aligned for 32bit dma transfer
  uint8_t __attribute__ ((aligned (4))) bitstream[Display::BITSTREAM_LENGTH];
  const uint32_t bitstream_addr = (uint32_t)bitstream;

  // *must* be in SRAM, since it could fire while flash is being erased/written
  static void __isr __no_inline_not_in_flash_func(_dma_complete)(void) {
    dma_hw->ints1 = 1u << dma_channel; // dma_channel_acknowledge_irq0(dma_channel);
    dma_hw->ch[dma_channel].al3_read_addr_trig = (uintptr_t) &bitstream; // dma_channel_set_read_addr(dma_channel, &bitstream, true);
  }

  Display* Display::widget = nullptr;
  PIO Display::bitstream_pio = pio0;
  uint Display::bitstream_sm = 0;
  uint Display::bitstream_sm_offset = 0;

  Display::~Display() {
    // Abort any in-progress DMA transfer (no RP2040-E13? We're on RP2350)
    dma_channel_abort(dma_ctrl_channel);
    dma_channel_abort(dma_channel);

    // Stop the bitstream SM
    pio_sm_set_enabled(bitstream_pio, bitstream_sm, false);

    // Make sure the display is off by turning off the column drivers
    const uint pins_to_set = 1 << COLUMN_BLANK;
    pio_sm_set_pins_with_mask(bitstream_pio, bitstream_sm, pins_to_set, pins_to_set);

    // Clock out data to turn off the row drivers
    gpio_put(ROW_DATA, false);
    for(uint32_t i = 0; i < ROW_COUNT; i++) {
      sleep_us(10);
      gpio_put(ROW_DATA_CLOCK, true);
      sleep_us(10);
      gpio_put(ROW_DATA_CLOCK, false);
    }

    dma_channel_unclaim(dma_channel);
    dma_channel_unclaim(dma_ctrl_channel);
    pio_sm_unclaim(bitstream_pio, bitstream_sm);
    pio_remove_program(bitstream_pio, &widget_program, bitstream_sm_offset);
 }

  void Display::init() {
    // for each row:
    //   for each bcd frame:
    //            0: 00001111                                     // row data & clock
    //            1: 00011111                                     // row pixel count (minus one)
    //      2  - 40: xxxxxxxv, xxxxxxxv, xxxxxxxv, ...            // pixel data)
    //      41 - 43: xxxxxxxx, xxxxxxxx, xxxxxxxx                 // dummy bytes to dword align
    //      44 - 47: tttttttt, tttttttt, tttttttt, tttttttt       // bcd tick count (0-65536)
    //
    //  .. and back to the start

    // initialise the bcd timing values and row selects in the bitstream
    for(uint8_t row = 0; row < ROW_COUNT; row++) {
      for(uint8_t frame = 0; frame < BCD_FRAME_COUNT; frame++) {
        // find the offset of this row and frame in the bitstream
        uint8_t *p = &bitstream[(row * ROW_BYTES) + (BCD_FRAME_BYTES * frame)];

        if(frame == 0) {
          if(row == 0)
            p[ 0] = 0b1101;  // row data high, toggle clock low, then high
          else
            p[ 0] = 0b1000;  // row data low, toggle clock low, then high
        }
        else {
          p[ 0] = 0b0000;    // row data low, clock low
        }

        // rows outside the screen's height are vblank to avoid ghosting
        if(row >= HEIGHT) {
          p[0] = 0b1000;
        }
        p[ 1] = WIDTH - 1;                  // row pixel count

        // set the number of bcd ticks for this frame
        uint32_t bcd_ticks = 1 << (frame + 1);
        p[BCD_FRAME_BYTES - 4] = (bcd_ticks &       0xff) >>  0;
        p[BCD_FRAME_BYTES - 3] = (bcd_ticks &     0xff00) >>  8;
        p[BCD_FRAME_BYTES - 2] = (bcd_ticks &   0xff0000) >> 16;
        p[BCD_FRAME_BYTES - 1] = (bcd_ticks & 0xff000000) >> 24;
      }
    }

    gpio_init(COLUMN_CLOCK); gpio_set_dir(COLUMN_CLOCK, GPIO_OUT); gpio_put(COLUMN_CLOCK, false);
    gpio_init(COLUMN_DATA);  gpio_set_dir(COLUMN_DATA, GPIO_OUT);  gpio_put(COLUMN_DATA, false);
    gpio_init(COLUMN_LATCH); gpio_set_dir(COLUMN_LATCH, GPIO_OUT); gpio_put(COLUMN_LATCH, false);
    gpio_init(COLUMN_BLANK); gpio_set_dir(COLUMN_BLANK, GPIO_OUT); gpio_put(COLUMN_BLANK, true);

    // initialise the row select, and set them to a non-visible row to avoid flashes during setup
    gpio_init(ROW_DATA); gpio_set_dir(ROW_DATA, GPIO_OUT); gpio_put(ROW_DATA, false);
    gpio_init(ROW_DATA_CLOCK); gpio_set_dir(ROW_DATA_CLOCK, GPIO_OUT); gpio_put(ROW_DATA_CLOCK, true);

    sleep_ms(100);

    // Clock out data to turn off the row drivers
    gpio_put(ROW_DATA, false);
    for(uint32_t i = 0; i < ROW_COUNT; i++) {
      sleep_us(10);
      gpio_put(ROW_DATA_CLOCK, true);
      sleep_us(10);
      gpio_put(ROW_DATA_CLOCK, false);
    }

    // configure full output current in register 2
    uint16_t reg1 = 0b1111111111001110;

    // clock the register value to the first 9 driver chips
    for(uint32_t j = 0; j < 9; j++) {
      for(uint32_t i = 0; i < 16; i++) {
        if(reg1 & (1U << (16 - 1 - i))) {
          gpio_put(COLUMN_DATA, true);
        }else{
          gpio_put(COLUMN_DATA, false);
        }
        sleep_us(10);
        gpio_put(COLUMN_CLOCK, true);
        sleep_us(10);
        gpio_put(COLUMN_CLOCK, false);
      }
    }

    // clock the last chip and latch the value
    for(uint32_t i = 0; i < 16; i++) {
      if(reg1 & (1U << (16 - 1 - i))) {
        gpio_put(COLUMN_DATA, true);
      }else{
        gpio_put(COLUMN_DATA, false);
      }

      sleep_us(10);
      gpio_put(COLUMN_CLOCK, true);
      sleep_us(10);
      gpio_put(COLUMN_CLOCK, false);

      if(i == 4) {
        gpio_put(COLUMN_LATCH, true);
      }
    }
    gpio_put(COLUMN_LATCH, false);

    // reapply the blank as the above seems to cause a slight glow.
    // Note, this will produce a brief flash if a visible row is selected (which it shouldn't be)
    gpio_put(COLUMN_BLANK, false);
    sleep_us(10);
    gpio_put(COLUMN_BLANK, true);

    // setup the pio if it has not previously been set up
    bitstream_pio = pio2;
    bitstream_sm = pio_claim_unused_sm(bitstream_pio, true);
    bitstream_sm_offset = pio_add_program(bitstream_pio, &widget_program);

    pio_gpio_init(bitstream_pio, COLUMN_CLOCK);
    pio_gpio_init(bitstream_pio, COLUMN_DATA);
    pio_gpio_init(bitstream_pio, COLUMN_LATCH);
    pio_gpio_init(bitstream_pio, COLUMN_BLANK);

    pio_gpio_init(bitstream_pio, ROW_DATA);
    pio_gpio_init(bitstream_pio, ROW_DATA_CLOCK);

    // set the blank and row pins to be high, then set all led driving pins as outputs.
    // This order is important to avoid a momentary flash
    const uint pins_to_set = 1 << COLUMN_BLANK;
    pio_sm_set_pins_with_mask(bitstream_pio, bitstream_sm, pins_to_set, pins_to_set);
    pio_sm_set_consecutive_pindirs(bitstream_pio, bitstream_sm, COLUMN_CLOCK, 6, true);

    pio_sm_config c = widget_program_get_default_config(bitstream_sm_offset);

    // osr shifts right, autopull on, autopull threshold 8
    sm_config_set_out_shift(&c, true, true, 32);

    // configure out, set, and sideset pins
    sm_config_set_out_pins(&c, ROW_DATA, 2);
    sm_config_set_set_pins(&c, COLUMN_DATA, 3);
    sm_config_set_sideset_pins(&c, COLUMN_CLOCK);

    // join fifos as only tx needed (gives 8 deep fifo instead of 4)
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // setup dma transfer for pixel data to the pio
    dma_channel = dma_claim_unused_channel(true);
    dma_ctrl_channel = dma_claim_unused_channel(true);

    dma_channel_config ctrl_config = dma_channel_get_default_config(dma_ctrl_channel);
    channel_config_set_transfer_data_size(&ctrl_config, DMA_SIZE_32);
    channel_config_set_read_increment(&ctrl_config, false);
    channel_config_set_write_increment(&ctrl_config, false);
  
    dma_channel_configure(
      dma_ctrl_channel,
      &ctrl_config,
      // Don't chain DMA back to its trigger target, use al3_read_addr_trig instead
      &dma_hw->ch[dma_channel].al3_read_addr_trig,
      &bitstream_addr,
      1,
      false
    );

    dma_channel_config config = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_bswap(&config, false); // byte swap to reverse little endian
    channel_config_set_dreq(&config, pio_get_dreq(bitstream_pio, bitstream_sm, true));
    channel_config_set_chain_to(&config, dma_ctrl_channel); 

    dma_channel_configure(
      dma_channel,
      &config,
      &bitstream_pio->txf[bitstream_sm],
      NULL,
      BITSTREAM_LENGTH / 4,
      false);

    pio_sm_init(bitstream_pio, bitstream_sm, bitstream_sm_offset, &c);

    pio_sm_set_enabled(bitstream_pio, bitstream_sm, true);

    // start the control channel
    dma_start_channel_mask(1u << dma_ctrl_channel);
  }

  void Display::clear() {
    for(uint8_t y = 0; y < HEIGHT; y++) {
      for(uint8_t x = 0; x < WIDTH; x++) {
        set_pixel(x, y, 0, 0, 0);
      }
    }
  }

  void Display::set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if(x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;

    // make those coordinates sane
    x = (WIDTH - 1) - x;
    y = (HEIGHT - 1) - y;

    r = (r * this->brightness) >> 8;
    g = (g * this->brightness) >> 8;
    b = (b * this->brightness) >> 8;

    uint16_t gamma_r = GAMMA_12BIT[r];
    uint16_t gamma_g = GAMMA_12BIT[g];
    uint16_t gamma_b = GAMMA_12BIT[b];

    // for each row:
    //   for each bcd frame:
    //            0: 00011111                           // row pixel count (minus one)
    //      1  - 32: xxxxxbgr, xxxxxbgr, xxxxxbgr, ...  // pixel data
    //      33 - 35: xxxxxxxx, xxxxxxxx, xxxxxxxx       // dummy bytes to dword align
    //           36: xxxxrrrr                           // row select bits
    //      37 - 39: tttttttt, tttttttt, tttttttt       // bcd tick count (0-65536)
    //
    //  .. and back to the start

    // set the appropriate bits in the separate bcd frames
    for(uint8_t frame = 0; frame < BCD_FRAME_COUNT; frame++) {
      uint8_t *p = &bitstream[(y * ROW_BYTES) + (BCD_FRAME_BYTES * frame) + 2 + x];

      uint8_t red_bit = gamma_r & 0b1;
      uint8_t green_bit = gamma_g & 0b1;
      uint8_t blue_bit = gamma_b & 0b1;

      *p = (blue_bit << 0) | (green_bit << 1) | (red_bit << 2);

      gamma_r >>= 1;
      gamma_g >>= 1;
      gamma_b >>= 1;
    }
  }

  void Display::set_brightness(float value) {
    value = value < 0.0f ? 0.0f : value;
    value = value > 1.0f ? 1.0f : value;
    // Max brightness is - in fact - 256 since it's applied with:
    // result = (channel * brightness) >> 8
    // eg: (255 * 256) >> 8 == 255
    this->brightness = floorf(value * 256.0f);
  }

  float Display::get_brightness() {
    return this->brightness / 256.0f;
  }

  void Display::adjust_brightness(float delta) {
    this->set_brightness(this->get_brightness() + delta);
  }

  void Display::update() {
  }
