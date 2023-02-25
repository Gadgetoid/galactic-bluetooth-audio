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

        // Lookup tables
        fix15 sine_table[SAMPLE_COUNT];    // a table of sines for the FFT
        fix15 filter_window[SAMPLE_COUNT]; // a table of window values for the FFT

        // And here's where we'll copy those samples for FFT calculation
        fix15 fr[SAMPLE_COUNT];
        fix15 fi[SAMPLE_COUNT];

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
        };
        ~FIX_FFT();

        void update();
        float max_frequency();
        int get_scaled(unsigned int i, unsigned int scale);
        int get_scaled_fix15(unsigned int i, fix15 scale);
        fix15 get_scaled_as_fix15(unsigned int i, fix15 scale);
};