#include "lib/rgb.hpp"
#include "effect.hpp"

void RainbowFFT::update(int16_t *buffer16, size_t sample_count) {
    int16_t* fft_array = &fft.sample_array[SAMPLES_PER_AUDIO_BUFFER * (BUFFERS_PER_FFT_SAMPLE - 1)];
    memmove(fft.sample_array, &fft.sample_array[SAMPLES_PER_AUDIO_BUFFER], (BUFFERS_PER_FFT_SAMPLE - 1) * sizeof(uint16_t));

    for (auto i = 0u; i < SAMPLES_PER_AUDIO_BUFFER; i++) {
        fft_array[i] = buffer16[i];
    }

    fft.update();

    for (auto i = 0u; i < display.WIDTH; i++) {
        fix15 sample = std::min(float_to_fix15(max_sample_from_fft), fft.get_scaled_as_fix15(i + FFT_SKIP_BINS));
        uint8_t maxy = 0;

        for (int j = 0; j < HISTORY_LEN; ++j) {
            if (eq_history[i][j] > maxy) {
                maxy = eq_history[i][j];
            }
        }

#ifdef SCALE_SQRT
        fix15 subtract = subtract_step;
#endif
        for (auto y = 0; y < display.HEIGHT; y++) {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            if (sample > int_to_fix15(lower_threshold)) {
                r = (uint16_t)(palette_main[i].r);
                g = (uint16_t)(palette_main[i].g);
                b = (uint16_t)(palette_main[i].b);
#ifdef SCALE_LOGARITHMIC
                sample = multiply_fix15_unit(multiple, sample);
#else 
                sample = std::max(1, sample - subtract);
#ifdef SCALE_SQRT
                subtract += subtract_step;
#endif
#endif
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
                r = (uint16_t)(palette_main[i].r) >> 3;
                g = (uint16_t)(palette_main[i].g) >> 3;
                b = (uint16_t)(palette_main[i].b) >> 3;
            }
            display.set_pixel(i, display.HEIGHT - 1 - y, r, g, b);
        }
        if (maxy > 0) {
            RGB c = palette_peak[i];
            display.set_pixel(i, display.HEIGHT - 1 - maxy, c.r, c.g, c.b);
        }
    }
    history_idx = (history_idx + 1) % HISTORY_LEN;
}

void RainbowFFT::init(uint32_t sample_frequency) {
    printf("RainbowFFT: %ix%i\n", display.WIDTH, display.HEIGHT);

    history_idx = 0;

    fft.set_scale(display.HEIGHT * .318f);

    for(auto i = 0u; i < display.WIDTH; i++) {
        float h = float(i) / display.WIDTH;
        palette_peak[i] = RGB::from_hsv(h, 0.7f, 1.0f);
        palette_main[i] = RGB::from_hsv(h, 1.0f, 0.7f);
    }

    max_sample_from_fft = 4000.f + 130.f * display.HEIGHT;
    lower_threshold = 270 - 2 * display.HEIGHT;
#ifdef SCALE_LOGARITHMIC
    multiple = float_to_fix15(pow(max_sample_from_fft / lower_threshold, -1.f / (display.HEIGHT - 1)));
#elif defined(SCALE_SQRT)
    subtract_step = float_to_fix15((max_sample_from_fft - lower_threshold) * 2.f / (display.HEIGHT * (display.HEIGHT - 1)));
#elif defined(SCALE_LINEAR)
    subtract = float_to_fix15((max_sample_from_fft - lower_threshold) / (display.HEIGHT - 1));
#endif
}