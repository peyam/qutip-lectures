// Reed-Solomon RS(255,239) over GF(2^8), primitive polynomial 0x11D
// (spec 6.2), with the 39-byte shortening used by the AIW frame.
//
// Generator roots are alpha^(fcr + i), i = 0..15, primitive element alpha = 2.
// fcr = 0 matches the DVB-T / gr-dtv and Python `reedsolo` conventions.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace aiw {

class ReedSolomon {
public:
    static constexpr int N = 255;
    static constexpr int K = 239;
    static constexpr int NROOTS = N - K;  // 16
    static constexpr int T = NROOTS / 2;  // 8

    explicit ReedSolomon(int fcr = 0, unsigned prim_poly = 0x11D);

    // Full-length systematic encode: parity = remainder of msg(x) * x^16.
    void encode(std::span<const uint8_t, K> msg, std::span<uint8_t, NROOTS> parity) const;

    // Decodes a full 255-byte codeword in place.  Returns the number of
    // corrected symbols, or -1 if uncorrectable.  Positions < `pad` are
    // known-zero shortening bytes; a correction landing there is treated as
    // a decoding failure.
    int decode(std::span<uint8_t, N> cw, int pad = 0) const;

    // ---- shortened code used on air: 200 payload + 16 parity = 216 bytes ----
    static constexpr std::size_t SHORT_PAYLOAD = 200;
    static constexpr std::size_t SHORT_CW = SHORT_PAYLOAD + NROOTS;  // 216
    static constexpr int PAD = K - static_cast<int>(SHORT_PAYLOAD);   // 39

    void encode_shortened(std::span<const uint8_t, SHORT_PAYLOAD> payload,
                          std::span<uint8_t, SHORT_CW> codeword) const;

    // Prepends 39 zeros, decodes, and writes the corrected 216 bytes back.
    // Returns corrected symbol count or -1.
    int decode_shortened(std::span<uint8_t, SHORT_CW> codeword) const;

    uint8_t gf_mul(uint8_t a, uint8_t b) const {
        return (a && b) ? exp_[log_[a] + log_[b]] : 0;
    }

private:
    uint8_t gf_pow_alpha(int e) const { return exp_[((e % 255) + 255) % 255]; }
    uint8_t gf_inv(uint8_t a) const { return exp_[255 - log_[a]]; }

    int fcr_;
    std::array<uint8_t, 512> exp_{};
    std::array<int, 256> log_{};
    std::array<uint8_t, NROOTS + 1> gen_{};  // gen_[i] = coefficient of x^i
};

}  // namespace aiw
