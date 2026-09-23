// Square 256-QAM mapping and hard-decision slicing (spec 6.1).
//
// Levels I,Q in {-15,-13,...,15}; unit average power after dividing by
// sqrt(170).  byte = (idx_I << 4) | idx_Q with idx = (level + 15) / 2
// (natural binary, not Gray).
//
// Note: the rounding expression printed in the spec,
// 2*floor((x+1)/2) - 1, does not round to the nearest odd integer (it maps
// x = 0.5 to -1 and x = 2.5 to 1).  The nearest odd integer is
// 2*floor(x/2) + 1, which is what `slice_level` implements.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "config.hpp"

namespace aiw::qam256 {

inline constexpr float AVG_POWER = 170.0f;
inline const float SCALE = std::sqrt(AVG_POWER);  // 13.0384048

inline int slice_level(float x_scaled) {
    const int lvl = 2 * static_cast<int>(std::floor(x_scaled * 0.5f)) + 1;
    return std::clamp(lvl, -15, 15);
}

inline uint8_t slice(cf32 s) {
    const int i_hat = slice_level(s.real() * SCALE);
    const int q_hat = slice_level(s.imag() * SCALE);
    const auto idx_i = static_cast<uint8_t>((i_hat + 15) / 2);
    const auto idx_q = static_cast<uint8_t>((q_hat + 15) / 2);
    return static_cast<uint8_t>((idx_i << 4) | idx_q);
}

// Ideal (normalised) constellation point for a byte.
inline cf32 map(uint8_t b) {
    const int i_lvl = 2 * (b >> 4) - 15;
    const int q_lvl = 2 * (b & 0x0F) - 15;
    return {static_cast<float>(i_lvl) / SCALE, static_cast<float>(q_lvl) / SCALE};
}

}  // namespace aiw::qam256
