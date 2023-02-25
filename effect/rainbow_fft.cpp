#include "lib/rgb.hpp"
#include "effect.hpp"

void RainbowFFT::update(int16_t *buffer16, size_t sample_count) {
    int16_t* fft_array = &fft.sample_array[SAMPLES_PER_AUDIO_BUFFER * (BUFFERS_PER_FFT_SAMPLE - 1)];
    memmove(fft.sample_array, &fft.sample_array[SAMPLES_PER_AUDIO_BUFFER], (BUFFERS_PER_FFT_SAMPLE - 1) * sizeof(uint16_t));

    for (auto i = 0u; i < sample_count; i++) {
        fft_array[i] = buffer16[i];
    }

    fft.update();

    for (auto i = 0u; i < display.get_width(); i++) {
        fix15 sample = std::min(float_to_fix15(max_sample_from_fft), fft.get_scaled_as_fix15(i + FFT_SKIP_BINS, loudness_adjust[i]));
        uint8_t maxy = 0;

        for (int j = 0; j < HISTORY_LEN; ++j) {
            if (eq_history[i][j] > maxy) {
                maxy = eq_history[i][j];
            }
        }

#ifdef SCALE_SQRT
        fix15 subtract = subtract_step;
#endif
        for (auto y = 0; y < display.get_height(); y++) {
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
                r = (uint16_t)(palette_main[i].r) >> 2;
                g = (uint16_t)(palette_main[i].g) >> 2;
                b = (uint16_t)(palette_main[i].b) >> 2;
            }
            display.set_pixel(i, display.get_height() - 1 - y, r, g, b);
        }
        if (maxy > 0) {
            RGB c = palette_peak[i];
            display.set_pixel(i, display.get_height() - 1 - maxy, c.r, c.g, c.b);
        }
    }
    history_idx = (history_idx + 1) % HISTORY_LEN;
}

void RainbowFFT::init_loudness(uint32_t sample_frequency) {
    float scale = float(display.get_height()) * .318f;

    for (int i = 0; i < display.get_width(); ++i) {
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

void RainbowFFT::init(uint32_t sample_frequency) {
    printf("RainbowFFT: %ix%i\n", display.get_width(), display.get_height());

    history_idx = 0;

    for(auto i = 0u; i < display.get_width(); i++) {
        float h = float(i) / display.get_width();
        palette_peak[i] = RGB::from_hsv(h, 0.7f, 1.0f);
        palette_main[i] = RGB::from_hsv(h, 1.0f, 0.7f);
    }

    max_sample_from_fft = 4000.f + 130.f * display.get_height();
    lower_threshold = 270 - 2 * display.get_height();
#ifdef SCALE_LOGARITHMIC
    multiple = float_to_fix15(pow(max_sample_from_fft / lower_threshold, -1.f / (display.get_height() - 1)));
#elif defined(SCALE_SQRT)
    subtract_step = float_to_fix15((max_sample_from_fft - lower_threshold) * 2.f / (display.get_height() * (display.get_height() - 1)));
#elif defined(SCALE_LINEAR)
    subtract = float_to_fix15((max_sample_from_fft - lower_threshold) / (display.get_height() - 1));
#endif

    init_loudness(sample_frequency);
}