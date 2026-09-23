// Radiometric in-band SNR estimator (spec 6.4).
//
// Runs on the DC-blocked input before frequency translation, where the
// signal occupies +60..+540 kHz and -540..-60 kHz is vacant:
//   p1 = |H1 x|^2 (signal + noise), p2 = |H2 x|^2 (noise reference)
//   exponential averages with time constant N_avg = 100 000 samples
//   SNR_dB = 10 log10(max(0, P1 - P2) / (P2 + eps))
// and the SNR is averaged over 10 000-sample blocks before being reported.
//
// Only the power averages are needed, so the band filters are evaluated on
// every `decim`-th sample (the average of a decimated |y|^2 is an unbiased
// estimate of the same power) to save CPU.
#pragma once

#include <cmath>
#include <vector>

#include "config.hpp"
#include "filters.hpp"

namespace aiw {

class SnrEstimator {
public:
    SnrEstimator(double fs, double avg_len, std::size_t report_block, int decim,
                 double f_lo = 60e3, double f_hi = 540e3, double transition = 30e3)
        : h1_(firdes::complex_band_pass(fs, f_lo, f_hi, transition)),
          h2_(firdes::complex_band_pass(fs, -f_hi, -f_lo, transition)),
          decim_(static_cast<std::size_t>(decim)),
          alpha_(static_cast<double>(decim) / avg_len),
          report_block_(report_block) {}

    // Returns true when a new block-averaged SNR value is available.
    bool process(const cf32* x, std::size_t n) {
        bool updated = false;
        y1_.clear();
        y2_.clear();
        h1_.process_decimated(x, n, decim_, y1_);
        h2_.process_decimated(x, n, decim_, y2_);
        for (std::size_t i = 0; i < y1_.size(); ++i) {
            p1_ += alpha_ * (static_cast<double>(std::norm(y1_[i])) - p1_);
            p2_ += alpha_ * (static_cast<double>(std::norm(y2_[i])) - p2_);
            const double psig = std::max(0.0, p1_ - p2_);
            block_acc_ += psig / (p2_ + 1e-20);
            ++block_n_;
            if (block_n_ * decim_ >= report_block_) {
                const double ratio = block_acc_ / static_cast<double>(block_n_);
                snr_db_ = 10.0 * std::log10(std::max(ratio, 1e-12));
                block_acc_ = 0.0;
                block_n_ = 0;
                updated = true;
            }
        }
        return updated;
    }

    double snr_db() const { return snr_db_; }
    double signal_power() const { return std::max(0.0, p1_ - p2_); }
    double noise_power() const { return p2_; }

private:
    FirFilter<std::complex<float>> h1_, h2_;
    std::size_t decim_;
    double alpha_;
    std::size_t report_block_;
    std::vector<cf32> y1_, y2_;
    double p1_ = 0.0, p2_ = 0.0;
    double block_acc_ = 0.0;
    std::size_t block_n_ = 0;
    double snr_db_ = NAN;
};

}  // namespace aiw
