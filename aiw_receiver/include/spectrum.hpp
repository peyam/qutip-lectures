// Averaged power spectrum for the dashboard: 1024-point Hann-windowed FFT,
// exponentially averaged, fft-shifted so bin 0 is -Fs/2.
#pragma once

#include <cmath>
#include <complex>
#include <vector>

#include "config.hpp"
#include "filters.hpp"

namespace aiw {

// In-place iterative radix-2 FFT (n must be a power of two).
inline void fft_inplace(std::vector<std::complex<float>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * PI / static_cast<double>(len);
        const std::complex<float> wl(static_cast<float>(std::cos(ang)), static_cast<float>(std::sin(ang)));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

class SpectrumEstimator {
public:
    explicit SpectrumEstimator(std::size_t n = 1024, float avg = 0.1f)
        : n_(n), avg_(avg), win_(n), buf_(n), psd_(n, 0.0f) {
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            win_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * PI * static_cast<double>(i) / static_cast<double>(n)));
            s += win_[i];
        }
        // Tone-calibrated: a complex tone of amplitude A reads A^2 (dBFS).
        norm_ = static_cast<float>(1.0 / (s * s));
    }

    // Uses the first n samples of each call (one FFT per input chunk).
    void process(const cf32* x, std::size_t count) {
        if (count < n_) return;
        for (std::size_t i = 0; i < n_; ++i) buf_[i] = x[i] * win_[i];
        fft_inplace(buf_);
        const float a = first_ ? 1.0f : avg_;
        first_ = false;
        for (std::size_t i = 0; i < n_; ++i) {
            const std::size_t k = (i + n_ / 2) % n_;  // fftshift
            psd_[i] += a * (std::norm(buf_[k]) * norm_ - psd_[i]);
        }
    }

    // dBFS: power relative to a full-scale (|x| = 1) complex tone.
    void psd_db(std::vector<float>& out) const {
        out.resize(n_);
        for (std::size_t i = 0; i < n_; ++i) out[i] = 10.0f * std::log10(psd_[i] + 1e-20f);
    }

private:
    std::size_t n_;
    float avg_;
    float norm_ = 1.0f;
    bool first_ = true;
    std::vector<float> win_;
    std::vector<std::complex<float>> buf_;
    std::vector<float> psd_;
};

}  // namespace aiw
