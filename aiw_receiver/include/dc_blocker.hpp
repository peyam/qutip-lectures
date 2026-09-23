// Single-pole DC notch (spec 4.1): y[n] = x[n] - x[n-1] + a * y[n-1],
// a = 1 - 1/L, applied independently to I and Q.
#pragma once

#include "config.hpp"

namespace aiw {

class DcBlocker {
public:
    explicit DcBlocker(double length = 32.0) : a_(static_cast<float>(1.0 - 1.0 / length)) {}

    void process(const cf32* in, cf32* out, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            const cf32 x = in[i];
            const cf32 y = x - x_prev_ + a_ * y_prev_;
            x_prev_ = x;
            y_prev_ = y;
            out[i] = y;
        }
    }

    float alpha() const { return a_; }

private:
    float a_;
    cf32 x_prev_{0.0f, 0.0f};
    cf32 y_prev_{0.0f, 0.0f};
};

}  // namespace aiw
