#include "lib/rgb.hpp"
#include "effect.hpp"

void ClassicFFT::update(int16_t *buffer16, size_t sample_count) {
    int16_t* fft_array = &fft.sample_array[SAMPLES_PER_AUDIO_BUFFER * (BUFFERS_PER_FFT_SAMPLE - 1)];
    memmove(fft.sample_array, &fft.sample_array[SAMPLES_PER_AUDIO_BUFFER], (BUFFERS_PER_FFT_SAMPLE - 1) * sizeof(uint16_t));

    for (auto i = 0u; i < SAMPLES_PER_AUDIO_BUFFER; i++) {
        fft_array[i] = buffer16[i];
    }

    fft.update();

    for (auto i = 0u; i < width; i++) {
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
        for (auto y = 0; y < height; y++) {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            if (sample > int_to_fix15(lower_threshold)) {
                r = (uint16_t)(palette[y].r);
                g = (uint16_t)(palette[y].g);
                b = (uint16_t)(palette[y].b);
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
                r = std::min((uint16_t)(palette[y].r), int_sample);
                g = std::min((uint16_t)(palette[y].g), int_sample);
                b = std::min((uint16_t)(palette[y].b), int_sample);
                eq_history[i][history_idx] = y;
                sample = 0;
                if (maxy < y) {
                    maxy = y;
                }
            } else if (y < maxy) {
                r = (uint16_t)(palette[y].r) >> 2;
                g = (uint16_t)(palette[y].g) >> 2;
                b = (uint16_t)(palette[y].b) >> 2;
            }
            display.set_pixel(i, height - 1 - y, r, g, b);
        }
        if (maxy > 0) {
            RGB c = palette[height - 1];
            display.set_pixel(i, height - 1 - maxy, c.r, c.g, c.b);
        }
    }
    history_idx = (history_idx + 1) % HISTORY_LEN;
}

void ClassicFFT::init(uint32_t sample_frequency) {
    printf("ClassicFFT: %ix%i\n", width, height);

    history_idx = 0;

    fft.set_scale(height * .318f);

    for(auto i = 0u; i < height; i++) {
        int n = floor(i / 4) * 4;
        float h = 0.4 * float(n) / height;
        h = 0.333 - h;
        palette[i] = RGB::from_hsv(h, 1.0f, 1.0f);
    }

    max_sample_from_fft = 4000.f + 130.f * height;
    lower_threshold = 270 - 2 * height;
#ifdef SCALE_LOGARITHMIC
    multiple = float_to_fix15(pow(max_sample_from_fft / lower_threshold, -1.f / (height - 1)));
#elif defined(SCALE_SQRT)
    subtract_step = float_to_fix15((max_sample_from_fft - lower_threshold) * 2.f / (height * (height - 1)));
#elif defined(SCALE_LINEAR)
    subtract = float_to_fix15((max_sample_from_fft - lower_threshold) / (height - 1));
#endif
}