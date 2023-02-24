#include <math.h>

#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"


#include "galactic_unicorn.pio.h"

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
//            0: 00110110                           // row pixel count (minus one)
//      1  - 53: xxxxxbgr, xxxxxbgr, xxxxxbgr, ...  // pixel data
//      54 - 55: xxxxxxxx, xxxxxxxx                 // dummy bytes to dword align
//           56: xxxxrrrr                           // row select bits
//      57 - 59: tttttttt, tttttttt, tttttttt       // bcd tick count (0-65536)
//
//  .. and back to the start

//static uint16_t r_gamma_lut[256] = {0};

static uint32_t dma_channel;
static uint32_t dma_ctrl_channel;

PIO Display::bitstream_pio = pio0;
uint Display::bitstream_sm = 0;
uint Display::bitstream_sm_offset = 0;

static uint16_t gamma_lut[256] = {0};

Display::~Display() {
  dma_channel_unclaim(dma_ctrl_channel); // This works now the teardown behaves correctly
  dma_channel_unclaim(dma_channel); // This works now the teardown behaves correctly
  pio_sm_unclaim(bitstream_pio, bitstream_sm);
  pio_remove_program(bitstream_pio, &galactic_unicorn_program, bitstream_sm_offset);
}

uint16_t Display::light() {
  adc_select_input(2);
  return adc_read();
}

void Display::init() {
  float gamma = 1.8f;
  // create 14-bit gamma luts
  for(uint16_t v = 0; v < 256; v++) {
    // gamma correct the provided 0-255 brightness value onto a
    // 0-65535 range for the pwm counter
    gamma_lut[v] = (uint16_t)(powf((float)(v) / 255.0f, gamma) * (float(1U << (BCD_FRAME_COUNT)) - 1.0f) + 0.5f);
  }

  // for each row:
  //   for each bcd frame:
  //            0: 00110110                           // row pixel count (minus one)
  //      1  - 53: xxxxxbgr, xxxxxbgr, xxxxxbgr, ...  // pixel data
  //      54 - 55: xxxxxxxx, xxxxxxxx                 // dummy bytes to dword align
  //           56: xxxxrrrr                           // row select bits
  //      57 - 59: tttttttt, tttttttt, tttttttt       // bcd tick count (0-65536)
  //
  //  .. and back to the start

  // initialise the bcd timing values and row selects in the bitstream
  for(uint8_t row = 0; row < HEIGHT; row++) {
    for(uint8_t frame = 0; frame < BCD_FRAME_COUNT; frame++) {
      // find the offset of this row and frame in the bitstream
      uint8_t *p = &bitstream[row * ROW_BYTES + (BCD_FRAME_BYTES * frame)];

      p[ 0] = WIDTH - 1;               // row pixel count
      p[ 1] = row;                     // row select

      // set the number of bcd ticks for this frame
      uint32_t bcd_ticks = (1 << frame);
      p[56] = (bcd_ticks &       0xff) >>  0;
      p[57] = (bcd_ticks &     0xff00) >>  8;
      p[58] = (bcd_ticks &   0xff0000) >> 16;
      p[59] = (bcd_ticks & 0xff000000) >> 24;
    }
  }

  // setup light sensor adc
  adc_init();
  adc_gpio_init(LIGHT_SENSOR);

  gpio_init(COLUMN_CLOCK); gpio_set_dir(COLUMN_CLOCK, GPIO_OUT); gpio_put(COLUMN_CLOCK, false);
  gpio_init(COLUMN_DATA); gpio_set_dir(COLUMN_DATA, GPIO_OUT); gpio_put(COLUMN_DATA, false);
  gpio_init(COLUMN_LATCH); gpio_set_dir(COLUMN_LATCH, GPIO_OUT); gpio_put(COLUMN_LATCH, false);
  gpio_init(COLUMN_BLANK); gpio_set_dir(COLUMN_BLANK, GPIO_OUT); gpio_put(COLUMN_BLANK, true);

  // initialise the row select, and set them to a non-visible row to avoid flashes during setup
  gpio_init(ROW_BIT_0); gpio_set_dir(ROW_BIT_0, GPIO_OUT); gpio_put(ROW_BIT_0, true);
  gpio_init(ROW_BIT_1); gpio_set_dir(ROW_BIT_1, GPIO_OUT); gpio_put(ROW_BIT_1, true);
  gpio_init(ROW_BIT_2); gpio_set_dir(ROW_BIT_2, GPIO_OUT); gpio_put(ROW_BIT_2, true);
  gpio_init(ROW_BIT_3); gpio_set_dir(ROW_BIT_3, GPIO_OUT); gpio_put(ROW_BIT_3, true);

  sleep_ms(100);

  // configure full output current in register 2

  uint16_t reg1 = 0b1111111111001110;

  // clock the register value to the first 9 driver chips
  for(int j = 0; j < 9; j++) {
    for(int i = 0; i < 16; i++) {
      if(reg1 & (1U << (15 - i))) {
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
  for(int i = 0; i < 16; i++) {
    if(reg1 & (1U << (15 - i))) {
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

  gpio_init(MUTE); gpio_set_dir(MUTE, GPIO_OUT); gpio_put(MUTE, true);

  // setup button inputs
  gpio_init(SWITCH_A); gpio_pull_up(SWITCH_A);
  gpio_init(SWITCH_B); gpio_pull_up(SWITCH_B);
  gpio_init(SWITCH_C); gpio_pull_up(SWITCH_C);
  gpio_init(SWITCH_D); gpio_pull_up(SWITCH_D);

  gpio_init(SWITCH_SLEEP); gpio_pull_up(SWITCH_SLEEP);

  gpio_init(SWITCH_BRIGHTNESS_UP); gpio_pull_up(SWITCH_BRIGHTNESS_UP);
  gpio_init(SWITCH_BRIGHTNESS_DOWN); gpio_pull_up(SWITCH_BRIGHTNESS_DOWN);

  gpio_init(SWITCH_VOLUME_UP); gpio_pull_up(SWITCH_VOLUME_UP);
  gpio_init(SWITCH_VOLUME_DOWN); gpio_pull_up(SWITCH_VOLUME_DOWN);

  // setup the pio if it has not previously been set up
  bitstream_pio = pio1;
  bitstream_sm = pio_claim_unused_sm(bitstream_pio, true);
  bitstream_sm_offset = pio_add_program(bitstream_pio, &galactic_unicorn_program);

  pio_gpio_init(bitstream_pio, COLUMN_CLOCK);
  pio_gpio_init(bitstream_pio, COLUMN_DATA);
  pio_gpio_init(bitstream_pio, COLUMN_LATCH);
  pio_gpio_init(bitstream_pio, COLUMN_BLANK);

  pio_gpio_init(bitstream_pio, ROW_BIT_0);
  pio_gpio_init(bitstream_pio, ROW_BIT_1);
  pio_gpio_init(bitstream_pio, ROW_BIT_2);
  pio_gpio_init(bitstream_pio, ROW_BIT_3);

  // set the blank and row pins to be high, then set all led driving pins as outputs.
  // This order is important to avoid a momentary flash
  const uint pins_to_set = 1 << COLUMN_BLANK | 0b1111 << ROW_BIT_0;
  pio_sm_set_pins_with_mask(bitstream_pio, bitstream_sm, pins_to_set, pins_to_set);
  pio_sm_set_consecutive_pindirs(bitstream_pio, bitstream_sm, COLUMN_CLOCK, 8, true);

  pio_sm_config c = galactic_unicorn_program_get_default_config(bitstream_sm_offset);

  // osr shifts right, autopull on, autopull threshold 8
  sm_config_set_out_shift(&c, true, true, 32);

  // configure out, set, and sideset pins
  sm_config_set_out_pins(&c, ROW_BIT_0, 4);
  sm_config_set_set_pins(&c, COLUMN_DATA, 3);
  sm_config_set_sideset_pins(&c, COLUMN_CLOCK);

  // join fifos as only tx needed (gives 8 deep fifo instead of 4)
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // setup dma transfer for pixel data to the pio
  //if(unicorn == nullptr) {
    dma_channel = dma_claim_unused_channel(true);
    dma_ctrl_channel = dma_claim_unused_channel(true);
  //}
  dma_channel_config ctrl_config = dma_channel_get_default_config(dma_ctrl_channel);
  channel_config_set_transfer_data_size(&ctrl_config, DMA_SIZE_32);
  channel_config_set_read_increment(&ctrl_config, false);
  channel_config_set_write_increment(&ctrl_config, false);
  channel_config_set_chain_to(&ctrl_config, dma_channel);

  dma_channel_configure(
    dma_ctrl_channel,
    &ctrl_config,
    &dma_hw->ch[dma_channel].read_addr,
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

void Display::dma_safe_abort(uint channel) {
  // Tear down the DMA channel.
  // This is copied from: https://github.com/raspberrypi/pico-sdk/pull/744/commits/5e0e8004dd790f0155426e6689a66e08a83cd9fc
  uint32_t irq0_save = dma_hw->inte0 & (1u << channel);
  hw_clear_bits(&dma_hw->inte0, irq0_save);

  dma_hw->abort = 1u << channel;

  // To fence off on in-flight transfers, the BUSY bit should be polled
  // rather than the ABORT bit, because the ABORT bit can clear prematurely.
  while (dma_hw->ch[channel].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) tight_loop_contents();

  // Clear the interrupt (if any) and restore the interrupt masks.
  dma_hw->ints0 = 1u << channel;
  hw_set_bits(&dma_hw->inte0, irq0_save);
}

void Display::set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if(x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;

  // make those coordinates sane
  x = (WIDTH - 1) - x;
  y = (HEIGHT - 1) - y;

  r = (r * this->brightness) >> 8;
  g = (g * this->brightness) >> 8;
  b = (b * this->brightness) >> 8;

  uint16_t gamma_r = gamma_lut[r];
  uint16_t gamma_g = gamma_lut[g];
  uint16_t gamma_b = gamma_lut[b];

  // for each row:
  //   for each bcd frame:
  //            0: 00110110                           // row pixel count (minus one)
  //      1  - 53: xxxxxbgr, xxxxxbgr, xxxxxbgr, ...  // pixel data
  //      54 - 55: xxxxxxxx, xxxxxxxx                 // dummy bytes to dword align
  //           56: xxxxrrrr                           // row select bits
  //      57 - 59: tttttttt, tttttttt, tttttttt       // bcd tick count (0-65536)
  //
  //  .. and back to the start

  // set the appropriate bits in the separate bcd frames
  for(uint8_t frame = 0; frame < BCD_FRAME_COUNT; frame++) {
    uint8_t *p = &bitstream[y * ROW_BYTES + (BCD_FRAME_BYTES * frame) + 2 + x];

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
  this->brightness = floor(value * 256.0f);
}

float Display::get_brightness() {
  return this->brightness / 255.0f;
}

void Display::adjust_brightness(float delta) {
  this->set_brightness(this->get_brightness() + delta);
}

void update() {
  // do something here, probably do the FFT and write the display back buffer?
}