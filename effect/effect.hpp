#pragma once
#include <functional>
#include "displaybase.hpp"
#include "lib/fixed_fft.hpp"
#include "lib/rgb.hpp"

class Effect {
    public:
        DisplayBase &display;
        Effect(DisplayBase& display) : display(display) {};
        virtual void init(uint32_t sample_frequency);
        virtual void update(int16_t *buffer16, size_t sample_count);
};

class RainbowFFT : public Effect {
    private:
        // Number of FFT bins to skip on the left, the low frequencies tend to be pretty boring visually
        static constexpr unsigned int FFT_SKIP_BINS = 1;
        static constexpr unsigned int BUFFERS_PER_FFT_SAMPLE = 2;
        static constexpr unsigned int SAMPLES_PER_AUDIO_BUFFER = SAMPLE_COUNT / BUFFERS_PER_FFT_SAMPLE;
        static constexpr int HISTORY_LEN = 21; // About 0.25s
        uint history_idx;
        uint8_t eq_history[32][HISTORY_LEN];
        fix15 loudness_adjust[32];


        FIX_FFT fft;
        RGB palette_peak[32];
        RGB palette_main[32];

        float max_sample_from_fft;
        int lower_threshold;
#ifdef SCALE_LOGARITHMIC
        fix15 multiple;
#elif defined(SCALE_SQRT)
        fix15 subtract_step;
#elif defined(SCALE_LINEAR)
        fix15 subtract;
#else
#error "Choose a scale mode"
#endif

        struct LoudnessLookup {
            int freq;
            float multiplier;
        };

        // Amplitude to loudness lookup at 20 phons
        static constexpr LoudnessLookup loudness_lookup[] = {
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

        void init_loudness(uint32_t sample_frequency);

    public:
        RainbowFFT(DisplayBase& display) : Effect(display) {}
        void update(int16_t *buffer16, size_t sample_count) override;
        void init(uint32_t sample_frequency) override;
};
