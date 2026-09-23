// Thread-4 per-frame processing (spec 6.1 - 6.3): 256-QAM slicing, shortened
// RS(255,239) decoding and BER against the ground-truth payload.
#pragma once

#include <array>
#include <cstdint>

#include "config.hpp"
#include "constellation.hpp"
#include "fec_reed_solomon.hpp"

namespace aiw {

struct DecodeResult {
    int rs_corrected = 0;             // -1 = uncorrectable
    std::size_t pre_fec_bit_errors = 0;   // over all 216 bytes (1728 bits)
    std::size_t post_fec_bit_errors = 0;  // over the 200 payload bytes (1600 bits)
    std::array<uint8_t, PAYLOAD_SIZE> payload{};
};

class FrameDecoder {
public:
    FrameDecoder(const std::array<uint8_t, PAYLOAD_SIZE>& golden, int rs_fcr)
        : rs_(rs_fcr), golden_(golden) {
        rs_.encode_shortened(golden_, expected_cw_);
    }

    DecodeResult decode(const std::array<cf32, DATA_LENGTH_SYM>& symbols) const {
        DecodeResult r;
        std::array<uint8_t, CODEWORD_SIZE_SHORT> cw{};
        for (std::size_t i = 0; i < DATA_LENGTH_SYM; ++i) cw[i] = qam256::slice(symbols[i]);
        for (std::size_t i = 0; i < CODEWORD_SIZE_SHORT; ++i)
            r.pre_fec_bit_errors += static_cast<std::size_t>(__builtin_popcount(cw[i] ^ expected_cw_[i]));
        r.rs_corrected = rs_.decode_shortened(cw);
        // On failure the systematic bytes are passed through uncorrected.
        std::copy(cw.begin(), cw.begin() + PAYLOAD_SIZE, r.payload.begin());
        for (std::size_t i = 0; i < PAYLOAD_SIZE; ++i)
            r.post_fec_bit_errors += static_cast<std::size_t>(__builtin_popcount(r.payload[i] ^ golden_[i]));
        return r;
    }

    const std::array<uint8_t, CODEWORD_SIZE_SHORT>& expected_codeword() const { return expected_cw_; }

private:
    ReedSolomon rs_;
    std::array<uint8_t, PAYLOAD_SIZE> golden_;
    std::array<uint8_t, CODEWORD_SIZE_SHORT> expected_cw_{};
};

}  // namespace aiw
