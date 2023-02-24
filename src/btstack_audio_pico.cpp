/*
 * Copyright (C) 2022 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL BLUEKITCHEN
 * GMBH OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */

#define BTSTACK_FILE__ "btstack_audio_pico.c"

/*
 *  btstack_audio_pico.c
 *
 *  Implementation of btstack_audio.h using pico_i2s
 *
 */

#include "btstack_config.h"

#include "btstack_debug.h"
#include "btstack_audio.h"
#include "btstack_run_loop.h"

#include <stddef.h>
#include <hardware/dma.h>

#include "pico/audio_i2s.h"
#include "pico/stdlib.h"

#include "display.hpp"
#include "fixed_fft.hpp"

#define DRIVER_POLL_INTERVAL_MS 5
#define FFT_SKIP_BINS 8 // Number of FFT bins to skip on the left, the low frequencies tend to be pretty boring visually

constexpr unsigned int BUFFERS_PER_FFT_SAMPLE = 2;
constexpr unsigned int SAMPLES_PER_AUDIO_BUFFER = SAMPLE_COUNT / BUFFERS_PER_FFT_SAMPLE;

struct RGB {
    uint8_t r, g, b;

    constexpr RGB() : r(0), g(0), b(0) {}
    constexpr RGB(uint c) : r((c >> 16) & 0xff), g((c >> 8) & 0xff), b(c & 0xff) {}
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
        }
    }
};

Display display;
constexpr int HISTORY_LEN = 21; // About 0.25s
static uint history_idx = 0;
static uint8_t eq_history[display.WIDTH][HISTORY_LEN] = {{0}};

FIX_FFT fft;

RGB palette_peak[display.WIDTH];
RGB palette_main[display.WIDTH];

// client
static void (*playback_callback)(int16_t * buffer, uint16_t num_samples);

// timer to fill output ring buffer
static btstack_timer_source_t  driver_timer_sink;


static bool btstack_audio_pico_sink_active;

// from pico-playground/audio/sine_wave/sine_wave.c

static audio_format_t        btstack_audio_pico_audio_format;
static audio_buffer_format_t btstack_audio_pico_producer_format;
static audio_buffer_pool_t * btstack_audio_pico_audio_buffer_pool;
static uint8_t               btstack_audio_pico_channel_count;
static uint8_t               btstack_volume;
static uint8_t               btstack_last_sample_idx;

static audio_buffer_pool_t *init_audio(uint32_t sample_frequency, uint8_t channel_count) {

    // num channels requested by application
    btstack_audio_pico_channel_count = channel_count;

    // always use stereo
    btstack_audio_pico_audio_format.format = AUDIO_BUFFER_FORMAT_PCM_S16;
    btstack_audio_pico_audio_format.sample_freq = sample_frequency;
    btstack_audio_pico_audio_format.channel_count = 2;

    btstack_audio_pico_producer_format.format = &btstack_audio_pico_audio_format;
    btstack_audio_pico_producer_format.sample_stride = 2 * 2;

    btstack_last_sample_idx = 0;

    audio_buffer_pool_t * producer_pool = audio_new_producer_pool(&btstack_audio_pico_producer_format, 3, SAMPLES_PER_AUDIO_BUFFER); // todo correct size

    audio_i2s_config_t config;
    config.data_pin       = PICO_AUDIO_I2S_DATA_PIN;
    config.clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE;
    config.dma_channel    = (int8_t) dma_claim_unused_channel(true);
    config.pio_sm         = 0;

    // audio_i2s_setup claims the channel again https://github.com/raspberrypi/pico-extras/issues/48
    dma_channel_unclaim(config.dma_channel);
    const audio_format_t * output_format = audio_i2s_setup(&btstack_audio_pico_audio_format, &config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    bool ok = audio_i2s_connect(producer_pool);
    assert(ok);
    (void)ok;


    display.init();
    display.clear();
    display.set_pixel(0, 0, 255, 0, 0);

    for(auto i = 0u; i < display.WIDTH; i++) {
        float h = float(i) / display.WIDTH;
        palette_peak[i] = RGB::from_hsv(h, 0.7f, 1.0f);
        palette_main[i] = RGB::from_hsv(h, 1.0f, 0.7f);
    }

    return producer_pool;
}

static void btstack_audio_pico_sink_fill_buffers(void){
    while (true){
        audio_buffer_t * audio_buffer = take_audio_buffer(btstack_audio_pico_audio_buffer_pool, false);
        if (audio_buffer == NULL){
            break;
        }

        int16_t * buffer16 = (int16_t *) audio_buffer->buffer->bytes;
        (*playback_callback)(buffer16, audio_buffer->max_sample_count);

        int16_t* fft_array = &fft.sample_array[SAMPLES_PER_AUDIO_BUFFER * (BUFFERS_PER_FFT_SAMPLE - 1)];
        memmove(fft.sample_array, &fft.sample_array[SAMPLES_PER_AUDIO_BUFFER], (BUFFERS_PER_FFT_SAMPLE - 1) * sizeof(uint16_t));
        for (auto i = 0u; i < SAMPLE_COUNT; i++) {
            fft_array[i] = buffer16[i];
            // Apply volume after copying to FFT
            buffer16[i] = (int32_t(buffer16[i]) * int32_t(btstack_volume)) >> 8;
        }

        // duplicate samples for mono
        if (btstack_audio_pico_channel_count == 1){
            int16_t i;
            for (i = SAMPLE_COUNT - 1 ; i >= 0; i--){
                buffer16[2*i  ] = buffer16[i];
                buffer16[2*i+1] = buffer16[i];
            }
        }

        fft.update();
        float scale = float(display.HEIGHT) * .318;
        for (auto i = 0u; i < display.WIDTH; i++) {
            uint16_t sample = std::min((int16_t)(display.HEIGHT * 255), (int16_t)fft.get_scaled_fix15(i + FFT_SKIP_BINS, float_to_fix15(scale)));
            uint8_t maxy = 0;
            int maxj = -1;

            for (int j = 0; j < HISTORY_LEN; ++j) {
                if (eq_history[i][j] > maxy) {
                    maxy = eq_history[i][j];
                    maxj = j;
                }
            }

            for (auto y = 0; y < display.HEIGHT; y++) {
                uint8_t r = 0;
                uint8_t g = 0;
                uint8_t b = 0;
                if (sample > 255) {
                    r = (uint16_t)(palette_main[i].r);
                    g = (uint16_t)(palette_main[i].g);
                    b = (uint16_t)(palette_main[i].b);
                    sample -= 255;
                }
                else if (sample > 0) {
                    r = std::min((uint16_t)(palette_main[i].r), sample);
                    g = std::min((uint16_t)(palette_main[i].g), sample);
                    b = std::min((uint16_t)(palette_main[i].b), sample);
                    eq_history[i][history_idx] = y;
                    sample = 0;
                } else if (y < maxy) {
                    r = (uint16_t)(palette_main[i].r) >> 2;
                    g = (uint16_t)(palette_main[i].g) >> 2;
                    b = (uint16_t)(palette_main[i].b) >> 2;
                }
                display.set_pixel(i, display.HEIGHT - 1 - y, r, g, b);
            }
            if (maxj != history_idx && maxy > 0) {
                RGB c = palette_peak[i];
                display.set_pixel(i, display.HEIGHT - 1 - maxy, c.r, c.g, c.b);
            }
        }
        history_idx = (history_idx + 1) % HISTORY_LEN;

        audio_buffer->sample_count = audio_buffer->max_sample_count;
        give_audio_buffer(btstack_audio_pico_audio_buffer_pool, audio_buffer);
    }
}

static void driver_timer_handler_sink(btstack_timer_source_t * ts){

    // refill
    btstack_audio_pico_sink_fill_buffers();

    // re-set timer
    btstack_run_loop_set_timer(ts, DRIVER_POLL_INTERVAL_MS);
    btstack_run_loop_add_timer(ts);
}

static int btstack_audio_pico_sink_init(
    uint8_t channels,
    uint32_t samplerate, 
    void (*playback)(int16_t * buffer, uint16_t num_samples)
){
    btstack_assert(playback != NULL);
    btstack_assert(channels != 0);

    gpio_init(22); gpio_set_dir(22, GPIO_OUT); gpio_put(22, true);

    playback_callback  = playback;

    btstack_audio_pico_audio_buffer_pool = init_audio(samplerate, channels);

    return 0;
}

static void btstack_audio_pico_sink_set_volume(uint8_t volume){
    btstack_volume = volume;
}

static void btstack_audio_pico_sink_start_stream(void){
    display.set_pixel(0, 2, 0, 255, 0);

    // pre-fill HAL buffers
    btstack_audio_pico_sink_fill_buffers();

    // start timer
    btstack_run_loop_set_timer_handler(&driver_timer_sink, &driver_timer_handler_sink);
    btstack_run_loop_set_timer(&driver_timer_sink, DRIVER_POLL_INTERVAL_MS);
    btstack_run_loop_add_timer(&driver_timer_sink);

    // state
    btstack_audio_pico_sink_active = true;

    audio_i2s_set_enabled(true);
}

static void btstack_audio_pico_sink_stop_stream(void){
    display.set_pixel(0, 2, 0, 0, 0);

    audio_i2s_set_enabled(false);

    // stop timer
    btstack_run_loop_remove_timer(&driver_timer_sink);
    // state
    btstack_audio_pico_sink_active = false;
}

static void btstack_audio_pico_sink_close(void){
    // stop stream if needed
    if (btstack_audio_pico_sink_active){
        btstack_audio_pico_sink_stop_stream();
    }
}

static const btstack_audio_sink_t btstack_audio_pico_sink = {
    .init = &btstack_audio_pico_sink_init,
    .set_volume = &btstack_audio_pico_sink_set_volume,
    .start_stream = &btstack_audio_pico_sink_start_stream,
    .stop_stream = &btstack_audio_pico_sink_stop_stream,
    .close = &btstack_audio_pico_sink_close,
};

const btstack_audio_sink_t * btstack_audio_pico_sink_get_instance(void){
    return &btstack_audio_pico_sink;
}
