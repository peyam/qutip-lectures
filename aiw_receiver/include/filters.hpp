// Filter design helpers and streaming FIR / mixer primitives (spec 4.2, 4.4, 6.4).
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <type_traits>
#include <vector>

#include "config.hpp"

namespace aiw {

inline constexpr double PI = 3.14159265358979323846;

namespace firdes {

// Number of taps for a Hamming-windowed sinc with the given transition width
// (same estimate as GNU Radio's firdes: N = 3.3 / (dF / Fs)), forced odd.
inline int hamming_ntaps(double fs, double transition_hz) {
    int n = static_cast<int>(std::ceil(3.3 * fs / transition_hz));
    return n | 1;
}

// Windowed-sinc low-pass, unity DC gain.  `cutoff_hz` is the -6 dB point.
inline std::vector<float> low_pass(double fs, double cutoff_hz, double transition_hz) {
    const int n = hamming_ntaps(fs, transition_hz);
    const int m = (n - 1) / 2;
    const double wc = 2.0 * PI * cutoff_hz / fs;
    std::vector<double> h(static_cast<std::size_t>(n));
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const int k = i - m;
        const double sinc = (k == 0) ? wc / PI : std::sin(wc * k) / (PI * k);
        const double w = 0.54 - 0.46 * std::cos(2.0 * PI * i / (n - 1));
        h[static_cast<std::size_t>(i)] = sinc * w;
        sum += sinc * w;
    }
    std::vector<float> out(h.size());
    for (std::size_t i = 0; i < h.size(); ++i) out[i] = static_cast<float>(h[i] / sum);
    return out;
}

// Complex band-pass [f_lo, f_hi] (may be negative frequencies) obtained by
// modulating a low-pass prototype to the band centre.
inline std::vector<std::complex<float>> complex_band_pass(double fs, double f_lo, double f_hi,
                                                          double transition_hz) {
    const auto lp = low_pass(fs, 0.5 * (f_hi - f_lo), transition_hz);
    const double fc = 0.5 * (f_hi + f_lo);
    const int m = static_cast<int>(lp.size() - 1) / 2;
    std::vector<std::complex<float>> bp(lp.size());
    for (std::size_t i = 0; i < lp.size(); ++i) {
        const double ph = 2.0 * PI * fc * (static_cast<int>(i) - m) / fs;
        bp[i] = std::complex<float>(static_cast<float>(lp[i] * std::cos(ph)),
                                    static_cast<float>(lp[i] * std::sin(ph)));
    }
    return bp;
}

// Root-raised-cosine, identical to GNU Radio's firdes::root_raised_cosine
// (ntaps forced odd, taps scaled to sum to `gain`).  This is the closed form
// of the h_rrc(t) expression in spec 4.4 with its removable singularities
// handled explicitly.
inline std::vector<float> root_raised_cosine(double gain, double sampling_freq, double symbol_rate,
                                             double alpha, int ntaps) {
    ntaps |= 1;
    const double spb = sampling_freq / symbol_rate;
    std::vector<double> taps(static_cast<std::size_t>(ntaps));
    double scale = 0.0;
    for (int i = 0; i < ntaps; ++i) {
        const double xindx = i - ntaps / 2;
        const double x1 = PI * xindx / spb;
        const double x2 = 4.0 * alpha * xindx / spb;
        double x3 = x2 * x2 - 1.0;
        double num, den;
        if (std::fabs(x3) >= 1e-6) {
            if (i != ntaps / 2)
                num = std::cos((1.0 + alpha) * x1) + std::sin((1.0 - alpha) * x1) / (4.0 * alpha * xindx / spb);
            else
                num = std::cos((1.0 + alpha) * x1) + (1.0 - alpha) * PI / (4.0 * alpha);
            den = x3 * PI;
        } else {
            if (alpha == 1.0) {
                taps[static_cast<std::size_t>(i)] = -1.0;
                scale += -1.0;
                continue;
            }
            x3 = (1.0 - alpha) * x1;
            const double x2b = (1.0 + alpha) * x1;
            num = std::sin(x2b) * (1.0 + alpha) * PI -
                  std::cos(x3) * ((1.0 - alpha) * PI * spb) / (4.0 * alpha * xindx) +
                  std::sin(x3) * spb * spb / (4.0 * alpha * xindx * xindx);
            den = -32.0 * PI * alpha * alpha * xindx / spb;
        }
        taps[static_cast<std::size_t>(i)] = 4.0 * alpha * num / den;
        scale += taps[static_cast<std::size_t>(i)];
    }
    std::vector<float> out(taps.size());
    for (std::size_t i = 0; i < taps.size(); ++i) out[i] = static_cast<float>(taps[i] * gain / scale);
    return out;
}

}  // namespace firdes

// Streaming FIR: y[n] = sum_k h[k] x[n-k].  TapT is float or complex<float>.
template <typename TapT>
class FirFilter {
public:
    explicit FirFilter(std::vector<TapT> taps) : taps_rev_(taps.rbegin(), taps.rend()) {
        hist_.assign(taps_rev_.size() - 1, cf32{});
    }

    std::size_t ntaps() const { return taps_rev_.size(); }

    // Filters n samples.  out may alias in.
    void process(const cf32* in, cf32* out, std::size_t n) {
        const std::size_t h = hist_.size();
        buf_.resize(h + n);
        std::copy(hist_.begin(), hist_.end(), buf_.begin());
        std::copy(in, in + n, buf_.begin() + static_cast<std::ptrdiff_t>(h));
        for (std::size_t i = 0; i < n; ++i) out[i] = dot(&buf_[i]);
        std::copy(buf_.end() - static_cast<std::ptrdiff_t>(h), buf_.end(), hist_.begin());
    }

    // Filters only every `decim`-th output sample (phase continues across
    // calls).  Appends results to `out`.
    void process_decimated(const cf32* in, std::size_t n, std::size_t decim, std::vector<cf32>& out) {
        const std::size_t h = hist_.size();
        buf_.resize(h + n);
        std::copy(hist_.begin(), hist_.end(), buf_.begin());
        std::copy(in, in + n, buf_.begin() + static_cast<std::ptrdiff_t>(h));
        std::size_t i = phase_;
        for (; i < n; i += decim) out.push_back(dot(&buf_[i]));
        phase_ = i - n;
        std::copy(buf_.end() - static_cast<std::ptrdiff_t>(h), buf_.end(), hist_.begin());
    }

private:
    // Split real/imag accumulation keeps the loop vectorisable.
    cf32 dot(const cf32* x) const {
        const std::size_t n = taps_rev_.size();
        const float* xf = reinterpret_cast<const float*>(x);
        if constexpr (std::is_same_v<TapT, float>) {
            float re = 0.0f, im = 0.0f;
            for (std::size_t k = 0; k < n; ++k) {
                re += taps_rev_[k] * xf[2 * k];
                im += taps_rev_[k] * xf[2 * k + 1];
            }
            return {re, im};
        } else {
            const float* tf = reinterpret_cast<const float*>(taps_rev_.data());
            float re = 0.0f, im = 0.0f;
            for (std::size_t k = 0; k < n; ++k) {
                const float xr = xf[2 * k], xi = xf[2 * k + 1];
                const float tr = tf[2 * k], ti = tf[2 * k + 1];
                re += tr * xr - ti * xi;
                im += tr * xi + ti * xr;
            }
            return {re, im};
        }
    }

    std::vector<TapT> taps_rev_;
    std::vector<cf32> hist_;
    std::vector<cf32> buf_;
    std::size_t phase_ = 0;
};

// Numerically controlled oscillator mixer: out = in * exp(j*2*pi*f/fs*n).
class Rotator {
public:
    Rotator(double freq_hz, double fs) {
        const double w = 2.0 * PI * freq_hz / fs;
        step_ = {std::cos(w), std::sin(w)};
    }

    void process(const cf32* in, cf32* out, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = in[i] * cf32(static_cast<float>(phasor_.real()), static_cast<float>(phasor_.imag()));
            phasor_ *= step_;
            if ((++count_ & 1023u) == 0) phasor_ /= std::abs(phasor_);
        }
    }

private:
    std::complex<double> step_;
    std::complex<double> phasor_{1.0, 0.0};
    std::size_t count_ = 0;
};

// Spec 4.2: mix the +offset carrier to 0 Hz, then low-pass to B/2 with a
// 0.1*B transition (Hamming windowed sinc).
class FreqXlatingFir {
public:
    FreqXlatingFir(double offset_hz, double fs, double occupied_bw)
        : mixer_(-offset_hz, fs), lpf_(firdes::low_pass(fs, occupied_bw / 2.0, 0.1 * occupied_bw)) {}

    void process(const cf32* in, cf32* out, std::size_t n) {
        mixer_.process(in, out, n);
        lpf_.process(out, out, n);
    }

    std::size_t ntaps() const { return lpf_.ntaps(); }

private:
    Rotator mixer_;
    FirFilter<float> lpf_;
};

}  // namespace aiw
