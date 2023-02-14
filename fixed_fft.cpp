/**
 * Hunter Adams (vha3@cornell.edu)
 * Reproduced and modified with explicit permission
 * 
 * Original code in action:
 * https://www.youtube.com/watch?v=8aibPy4yzCk
 *
 */
#include "fixed_fft.hpp"
#include <algorithm>

// Adapted from https://github.com/raspberrypi/pico-sdk/blob/master/src/host/pico_bit_ops/bit_ops.c
uint16_t __always_inline __revs(uint16_t v) {
    v = ((v & 0x5555u) << 1u) | ((v >> 1u) & 0x5555u);
    v = ((v & 0x3333u) << 2u) | ((v >> 2u) & 0x3333u);
    v = ((v & 0x0f0fu) << 4u) | ((v >> 4u) & 0x0f0fu);
    return ((v >> 8u) & 0x00ffu) | ((v & 0x00ffu) << 8u);
}

FIX_FFT::~FIX_FFT() {
}

int FIX_FFT::get_scaled(unsigned int i, unsigned int scale) {
    return fix15_to_int(multiply_fix15(fr[i], int_to_fix15(scale)));
}

void FIX_FFT::init() {

    // Populate Filter and Sine tables
    for (auto ii = 0u; ii < SAMPLE_COUNT; ii++) {
        // Full sine wave with period NUM_SAMPLES
        // Wolfram Alpha: Plot[(sin(2 * pi * (x / 1.0))), {x, 0, 1}]
        sine_table[ii] = float_to_fix15(0.5f * sin((M_PI * 2.0f) * ((float) ii) / (float)SAMPLE_COUNT));

        // This is a crude approximation of a Lanczos window.
        // Wolfram Alpha Comparison: Plot[0.5 * (1.0 - cos(2 * pi * (x / 1.0))), {x, 0, 1}], Plot[LanczosWindow[x - 0.5], {x, 0, 1}]
        filter_window[ii] = float_to_fix15(0.5f * (1.0f - cos((M_PI * 2.0f) * ((float) ii) / ((float)SAMPLE_COUNT))));
    }
}

void FIX_FFT::update() {
    float max_freq = 0;

    // Copy/window elements into a fixed-point array
    for (auto i = 0u; i < SAMPLE_COUNT; i++) {
        fr[i] = multiply_fix15(int_to_fix15((int)sample_array[i]), filter_window[i]);
        fi[i] = (fix15)0;
    }

    // Compute the FFT
    FFT();

    // Find the magnitudes
    for (auto i = 0u; i < (SAMPLE_COUNT / 2u); i++) {
        // get the approx magnitude
        fr[i] = abs(fr[i]); //>>9
        fi[i] = abs(fi[i]);
        // reuse fr to hold magnitude
        fr[i] = std::max(fr[i], fi[i]) + 
                multiply_fix15(std::min(fr[i], fi[i]), float_to_fix15(0.4f)); 

        // Keep track of maximum
        if (fr[i] > max_freq && i >= 5u) {
            max_freq = FIX_FFT::fr[i];
            max_freq_dex = i;
        }
    }
}

float FIX_FFT::max_frequency() {
    return max_freq_dex * (sample_rate / SAMPLE_COUNT);
}

void FIX_FFT::FFT() {
    // Bit Reversal Permutation
    // Bit reversal code below originally based on that found here: 
    // https://graphics.stanford.edu/~seander/bithacks.html#BitReverseObvious
    // https://en.wikipedia.org/wiki/Bit-reversal_permutation
    // Detail here: https://vanhunteradams.com/FFT/FFT.html#Single-point-transforms-(reordering)
    //
    // PH: Converted to stdlib functions and __revs so it doesn't hurt my eyes
    for (auto m = 1u; m < SAMPLE_COUNT - 1u; m++) {
        unsigned int mr = __revs(m) >> shift_amount;
        // don't swap that which has already been swapped
        if (mr <= m) continue;
        // swap the bit-reveresed indices
        std::swap(fr[m], fr[mr]);
        std::swap(fi[m], fi[mr]);
    }

    // Danielson-Lanczos
    // Adapted from code by:
    // Tom Roberts 11/8/89 and Malcolm Slaney 12/15/94 malcolm@interval.com
    // Detail here: https://vanhunteradams.com/FFT/FFT.html#Two-point-transforms
    // Length of the FFT's being combined (starts at 1)
    //
    // PH: Moved variable declarations to first-use so types are visually explicit.
    // PH: Removed div 2 on sine table values, have computed the sine table pre-divided.
    unsigned int L = 1;
    int k = log2_samples - 1;

    // While the length of the FFT's being combined is less than the number of gathered samples
    while (L < SAMPLE_COUNT) {
        // Determine the length of the FFT which will result from combining two FFT's
        int istep = L << 1;
        // For each element in the FFT's that are being combined
        for (auto m = 0u; m < L; ++m) { 
            // Lookup the trig values for that element
            int j = m << k; // index into sine_table
            fix15 wr =  sine_table[j + SAMPLE_COUNT / 4];
            fix15 wi = -sine_table[j];
            // i gets the index of one of the FFT elements being combined
            for (auto i = m; i < SAMPLE_COUNT; i += istep) {
                // j gets the index of the FFT element being combined with i
                int j = i + L;
                // compute the trig terms (bottom half of the above matrix)
                fix15 tr = multiply_fix15(wr, fr[j]) - multiply_fix15(wi, fi[j]);
                fix15 ti = multiply_fix15(wr, fi[j]) + multiply_fix15(wi, fr[j]);
                // divide ith index elements by two (top half of above matrix)
                fix15 qr = fr[i] >> 1;
                fix15 qi = fi[i] >> 1;
                // compute the new values at each index
                fr[j] = qr - tr;
                fi[j] = qi - ti;
                fr[i] = qr + tr;
                fi[i] = qi + ti;
            }    
        }
        --k;
        L = istep;
    }
}