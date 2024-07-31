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
#include "pico/multicore.h"
#include "pico/sync.h"

#ifndef HEADLESS
#include "display.hpp"
#include "effect.hpp"
#include "lib/fixed_fft.hpp"

#define DRIVER_POLL_INTERVAL_MS 5

Display display;
FIX_FFT fft;
RainbowFFT rainbow_fft(display, fft);
ClassicFFT classic_fft(display, fft);

std::vector<Effect *> effects;
unsigned int current_effect = 0;

#ifdef EFFECTS_ON_CORE1
constexpr int core1_stack_len = 512;
uint32_t core1_stack[512];
int16_t effect_buf[SAMPLE_COUNT] = {0};
#endif

#ifdef EFFECTS_ON_CORE1
auto_init_mutex(core1_effect_update);

void core1_entry() {
    while(1) {
        mutex_enter_blocking(&core1_effect_update);
        effects[0]->update(effect_buf, SAMPLE_COUNT);
        mutex_exit(&core1_effect_update);
    }
}
#endif

#else
// If display is disabled, we'll set the sample count to the same as fixed_fft.hpp does anyway.
constexpr unsigned int SAMPLE_COUNT = 1024u;
#define DRIVER_POLL_INTERVAL_MS 5

#endif

static constexpr unsigned int BUFFERS_PER_FFT_SAMPLE = 2;
static constexpr unsigned int SAMPLES_PER_AUDIO_BUFFER = SAMPLE_COUNT / BUFFERS_PER_FFT_SAMPLE;

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

    btstack_volume = 127;

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

#ifndef HEADLESS
    effects.push_back(&rainbow_fft);
    effects.push_back(&classic_fft);

    for(auto &effect : effects) {
        effect->init(sample_frequency);
    }

    display.init();
    display.clear();

#ifdef EFFECTS_ON_CORE1
    multicore_launch_core1_with_stack(core1_entry, core1_stack, core1_stack_len);
#endif

#endif

    return producer_pool;
}

static void btstack_audio_pico_sink_fill_buffers(void){
    while (true){
        audio_buffer_t * audio_buffer = take_audio_buffer(btstack_audio_pico_audio_buffer_pool, false);
        if (audio_buffer == NULL){
            break;
        }

#ifndef HEADLESS
        if (!gpio_get(Display::SWITCH_A)) {
            current_effect = 0;
        }

        if (!gpio_get(Display::SWITCH_B)) {
            current_effect = 1;
        }
#endif

        int16_t * buffer16 = (int16_t *) audio_buffer->buffer->bytes;
        (*playback_callback)(buffer16, audio_buffer->max_sample_count);

#ifndef HEADLESS
#ifndef EFFECTS_ON_CORE1
        effects[current_effect]->update(buffer16, SAMPLE_COUNT);
#endif

#ifdef EFFECTS_ON_CORE1
        mutex_enter_blocking(&core1_effect_update);
#endif
#endif

        for (auto i = 0u; i < SAMPLE_COUNT; i++) {
#ifndef HEADLESS
#ifdef EFFECTS_ON_CORE1
            effect_buf[i] = buffer16[i];
#endif
#endif
            buffer16[i] = (int32_t(buffer16[i]) * int32_t(btstack_volume)) >> 8;
        }
#ifndef HEADLESS
#ifdef EFFECTS_ON_CORE1
        mutex_exit(&core1_effect_update);
#endif
#endif

        // duplicate samples for mono
        if (btstack_audio_pico_channel_count == 1){
            int16_t i;
            for (i = SAMPLE_COUNT - 1 ; i >= 0; i--){
                buffer16[2*i  ] = buffer16[i];
                buffer16[2*i+1] = buffer16[i];
            }
        }

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
    audio_i2s_set_enabled(false);

    // stop timer
    btstack_run_loop_remove_timer(&driver_timer_sink);
    // state
    btstack_audio_pico_sink_active = false;

#ifndef HEADLESS
    display.clear();
#endif
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
