// Dual-rate AGC (spec 4.3).
//   y[n]   = x[n] * g[n]
//   e[n]   = |y[n]| - ref
//   g[n+1] = clamp(g[n] - gamma * e[n] * g[n], 0, max_gain)
//   gamma  = attack if e > 0 else decay
#pragma once

#include <algorithm>
#include <cmath>

#include "config.hpp"

namespace aiw {

class Agc {
public:
    Agc(float attack, float decay, float reference, float max_gain, float initial_gain = 1.0f)
        : attack_(attack), decay_(decay), ref_(reference), max_gain_(max_gain), gain_(initial_gain) {}

    void process(const cf32* in, cf32* out, std::size_t n) {
        float g = gain_;
        for (std::size_t i = 0; i < n; ++i) {
            const cf32 y = in[i] * g;
            out[i] = y;
            const float e = std::abs(y) - ref_;
            const float gamma = e > 0.0f ? attack_ : decay_;
            g = std::clamp(g - gamma * e * g, 0.0f, max_gain_);
            // A gain of exactly zero can never recover under a multiplicative
            // update; keep a tiny floor.
            if (g < 1e-9f) g = 1e-9f;
        }
        gain_ = g;
    }

    float gain() const { return gain_; }

private:
    float attack_, decay_, ref_, max_gain_;
    float gain_;
};

}  // namespace aiw
