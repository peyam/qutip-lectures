// Two-pass UW-aided correction (spec 5.3, `AIW2_combined_correction_two_pass`).
//
// Pass 1 - carrier frequency / phase.  The chain has no carrier-tracking loop,
//   so the residual CFO is estimated from the two UWs of the segment:
//     coarse: lag-D autocorrelation of z[k] = r[k] u*[k] inside each UW
//             (unambiguous to +-pi/D rad/symbol),
//     fine:   phase difference between the leading and trailing UW
//             correlations (lag L + 216 symbols),
//   and the whole segment is de-rotated.
// Pass 2 - channel.  A P = 5 tap linear equalizer is fitted to both UWs by
//   Tikhonov-regularised least squares, w = (Y^H Y + lambda I)^-1 Y^H d,
//   applied to the segment, and the residual phase
//   theta = arg(sum s_hat u*) is removed.
//
// `delay` sets the equalizer's decision delay: the row for target u[k] is
// [r[k+delay], r[k+delay-1], ..., r[k+delay-4]].  delay = 0 is the purely
// causal form written in the spec; the default of 2 centres the taps so
// pre-cursor ISI (residual timing error, non-minimum-phase multipath) is
// also equalised.
#pragma once

#include <array>
#include <complex>
#include <vector>

#include "config.hpp"

namespace aiw {

struct EqualizerResult {
    std::array<cf32, DATA_LENGTH_SYM> data{};
    std::array<cf32, EQ_TAPS> taps{};
    double cfo_rad_per_sym = 0.0;
    double residual_phase = 0.0;
    double uw_mer_db = 0.0;      // UW-aided MER after equalisation
    double cond_estimate = 0.0;  // max/min eigenvalue of Y^H Y + lambda I
};

class TwoPassEqualizer {
public:
    TwoPassEqualizer(std::vector<cf32> uw, std::size_t data_len, int delay = 2, double lambda = 1e-4,
                     bool cfo_correction = true);

    // `seg` = SEGMENT_MARGIN + (2L + data_len) + SEGMENT_MARGIN symbols.
    bool process(const std::vector<cf32>& seg, EqualizerResult& out);

private:
    std::vector<cf32> uw_;
    std::size_t L_;
    std::size_t data_len_;
    int delay_;
    double lambda_;
    bool cfo_;
    std::vector<std::complex<double>> r_;  // working copy
};

}  // namespace aiw
