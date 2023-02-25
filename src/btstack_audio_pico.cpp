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
#define FFT_SKIP_BINS 1 // Number of FFT bins to skip on the left, the low frequencies tend to be pretty boring visually

constexpr unsigned int BUFFERS_PER_FFT_SAMPLE = 2;
constexpr unsigned int SAMPLES_PER_AUDIO_BUFFER = SAMPLE_COUNT / BUFFERS_PER_FFT_SAMPLE;

struct LoudnessLookup {
    int freq;
    float multiplier;
};

// Amplitude to loudness lookup at 20 phons
constexpr LoudnessLookup loudness_lookup[] = {
    { 20, 0.2232641215f },
    { 25, 0.241984271f },
    { 31, 0.263227165f },
    { 40, 0.2872737719f },
    { 50, 0.3124023743f },
    { 63, 0.341588386f },
    { 80, 0.3760105283f },
    { 100, 0.4133939644f },
    { 125, 0.4551661356f },
    { 160, 0.508001016f },
    { 200, 0.5632216277f },
    { 250, 0.6251953736f },
    { 315, 0.6971070059f },
    { 400, 0.7791195949f },
    { 500, 0.8536064874f },
    { 630, 0.9310986965f },
    { 800, 0.9950248756f },
    { 1000, 0.9995002499f },
    { 1250, 0.9319664492f },
    { 1600, 0.9345794393f },
    { 2000, 1.101928375f },
    { 2500, 1.300390117f },
    { 3150, 1.402524544f },
    { 4000, 1.321003963f },
    { 5000, 1.073537305f },
    { 6300, 0.7993605116f },
    { 8000, 0.6345177665f },
    { 10000, 0.5808887598f },
    { 12500, 0.6053268765f },
    { 20000, 0 }
};

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
static fix15 loudness_adjust[display.WIDTH];

FIX_FFT fft;

RGB palette_peak[display.WIDTH];
RGB palette_main[display.WIDTH];

// client
static void (*playback_callback)(int16_t * buffer, uint16_t num_samples);

static void init_loudness(uint32_t sample_frequency) {
    float scale = float(display.HEIGHT) * .318f;

    for (int i = 0; i < display.WIDTH; ++i) {
        int freq = (sample_frequency * 2) * (i + FFT_SKIP_BINS) / SAMPLE_COUNT;
        int j = 0;
        while (loudness_lookup[j+1].freq < freq) {
            ++j;
        }
        float t = float(freq - loudness_lookup[j].freq) / float(loudness_lookup[j+1].freq - loudness_lookup[j].freq);
        loudness_adjust[i] = float_to_fix15(scale * (t * loudness_lookup[j+1].multiplier + (1.f - t) * loudness_lookup[j].multiplier));
        printf("%d %d %f\n", i, freq, fix15_to_float(loudness_adjust[i]));
    }
}

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

    btstack_volume = 127;
    init_loudness(sample_frequency);

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

        constexpr float max_sample_from_fft = 4000.f + 100.f * display.HEIGHT;
        constexpr int lower_threshold = 270 - 2 * display.HEIGHT;
        constexpr fix15 multiple = float_to_fix15(pow(max_sample_from_fft / lower_threshold, -1.f / (display.HEIGHT - 1)));
        fft.update();
        for (auto i = 0u; i < display.WIDTH; i++) {
            fix15 sample = std::min(float_to_fix15(max_sample_from_fft), fft.get_scaled_as_fix15(i + FFT_SKIP_BINS, loudness_adjust[i]));
            uint8_t maxy = 0;

            for (int j = 0; j < HISTORY_LEN; ++j) {
                if (eq_history[i][j] > maxy) {
                    maxy = eq_history[i][j];
                }
            }

            for (auto y = 0; y < display.HEIGHT; y++) {
                uint8_t r = 0;
                uint8_t g = 0;
                uint8_t b = 0;
                if (sample > int_to_fix15(lower_threshold)) {
                    r = (uint16_t)(palette_main[i].r);
                    g = (uint16_t)(palette_main[i].g);
                    b = (uint16_t)(palette_main[i].b);
                    sample = multiply_fix15_unit(multiple, sample);
                }
                else if (sample > 0) {
                    uint16_t int_sample = (uint16_t)fix15_to_int(sample);
                    r = std::min((uint16_t)(palette_main[i].r), int_sample);
                    g = std::min((uint16_t)(palette_main[i].g), int_sample);
                    b = std::min((uint16_t)(palette_main[i].b), int_sample);
                    eq_history[i][history_idx] = y;
                    sample = 0;
                    if (maxy < y) {
                        maxy = y;
                    }
                } else if (y < maxy) {
                    r = (uint16_t)(palette_main[i].r) >> 2;
                    g = (uint16_t)(palette_main[i].g) >> 2;
                    b = (uint16_t)(palette_main[i].b) >> 2;
                }
                display.set_pixel(i, display.HEIGHT - 1 - y, r, g, b);
            }
            if (maxy > 0) {
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
