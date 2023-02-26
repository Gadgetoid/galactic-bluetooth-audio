#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cstring>

#include "pico/stdlib.h"

typedef signed int fix15;

// Helpers for 16.15 fixed-point arithmetic
constexpr __always_inline fix15 multiply_fix15(fix15 a, fix15 b) {return (fix15)(((signed long long)(a) * (signed long long)(b)) >> 15);}
constexpr __always_inline fix15 float_to_fix15(float a) {return (fix15)(a * 32768.0f);}
constexpr __always_inline float fix15_to_float(fix15 a) {return (float)(a) / 32768.0f;}
constexpr __always_inline fix15 int_to_fix15(int a) {return (fix15)(a << 15);}
constexpr __always_inline int fix15_to_int(fix15 a) {return (int)(a >> 15);}

// abs(a) must be <= 1
constexpr __always_inline fix15 multiply_fix15_unit(fix15 a, fix15 b) {
    int32_t bh = b >> 15;
    int32_t bl = b & 0x7fff;
    return ((a * bl) >> 15) + (a * bh);;
}

constexpr unsigned int SAMPLE_COUNT = 1024u;

class FIX_FFT {
    private:
        float sample_rate;

        unsigned int log2_samples;
        unsigned int shift_amount;

        // Loudness compensation lookup table
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

        // Lookup tables
        fix15 sine_table[SAMPLE_COUNT];    // a table of sines for the FFT
        fix15 filter_window[SAMPLE_COUNT]; // a table of window values for the FFT

        // And here's where we'll copy those samples for FFT calculation
        fix15 fr[SAMPLE_COUNT];
        fix15 fi[SAMPLE_COUNT];

        // Storage for loudness compensation
        fix15 loudness_adjust[SAMPLE_COUNT];

        int max_freq_dex = 0;
        
        void FFT();
        void init();
    public:
        int16_t sample_array[SAMPLE_COUNT];

        FIX_FFT() : FIX_FFT(44100.0f) {};
        FIX_FFT(float sample_rate) : sample_rate(sample_rate) {
                log2_samples = log2(SAMPLE_COUNT);
                shift_amount = 16u - log2_samples;

                memset(sample_array, 0, SAMPLE_COUNT * sizeof(int16_t));

                memset(fr, 0, SAMPLE_COUNT * sizeof(fix15));
                memset(fi, 0, SAMPLE_COUNT * sizeof(fix15));

                init();
                set_scale(1.0f);
        };
        ~FIX_FFT();

        void update();
        void set_scale(float scale);
        float max_frequency();
        int get_scaled(unsigned int i);
        int get_scaled_fix15(unsigned int i);
        fix15 get_scaled_as_fix15(unsigned int i);
};