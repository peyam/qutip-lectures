// Reference AIW transmitter + channel model.
//
// Generates the on-air waveform the receiver expects (UW | RS-coded 256-QAM
// payload | UW, RRC-shaped at 4 sps, placed at +300 kHz) and applies
// configurable impairments: carrier offset, sample-clock offset, fractional
// timing delay, multipath echo, DC offset, gain and AWGN.  Used by the
// `--source sim` mode and by the test-suite to exercise every verification
// gate of spec section 9 without hardware.
#pragma once

#include <array>
#include <atomic>
#include <complex>
#include <cstdint>
#include <deque>
#include <random>
#include <vector>

#include "config.hpp"

namespace aiw {

struct ChannelParams {
    double esn0_db = 40.0;         // Es/N0 after matched filtering; >= 200 disables noise
    double cfo_hz = 0.0;           // residual carrier offset after the +300 kHz shift
    double clock_ppm = 0.0;        // TX/RX sample-clock mismatch
    double timing_offset = 0.37;   // initial fractional delay (samples)
    cf32 echo{0.0f, 0.0f};         // complex gain of a one-symbol-delayed echo
    cf32 dc_offset{0.02f, -0.015f};  // relative to signal RMS
    float amplitude = 0.05f;       // overall scale (exercises the AGC)
    double if_offset_hz = FREQ_XLAT_OFFSET;
    uint64_t seed = 1;
};

class TxSimulator {
public:
    TxSimulator(std::vector<cf32> uw, std::array<uint8_t, 200> payload, ChannelParams ch,
                int rs_fcr = 0);

    // Produces the next n received samples.
    void generate(cf32* out, std::size_t n);

    // Channel changes requested from another thread (e.g. the dashboard);
    // applied at the start of the next generate() call.
    void request_esn0_db(double v) { req_esn0_.store(v); req_pending_.store(true); }
    void request_cfo_hz(double v) { req_cfo_.store(v); req_pending_.store(true); }
    double esn0_db() const { return req_esn0_.load(); }
    double cfo_hz() const { return req_cfo_.load(); }

    // The 216 bytes (payload + parity) sent in every frame.
    const std::array<uint8_t, CODEWORD_SIZE_SHORT>& codeword() const { return codeword_; }
    std::size_t frame_symbols() const { return frame_.size(); }

private:
    void apply_channel();    // derives noise and LO step from ch_
    void refill_baseband();  // appends one frame worth of shaped 4-sps samples
    cf32 interp(double t);   // cubic Lagrange interpolation into bb_

    std::vector<cf32> frame_;          // symbols of one segment
    std::array<uint8_t, CODEWORD_SIZE_SHORT> codeword_{};
    std::vector<float> rrc_;
    std::deque<cf32> sym_hist_;        // symbol history for the pulse-shaping FIR
    std::vector<cf32> bb_;             // shaped 4-sps baseband
    double bb_base_ = 0.0;             // absolute sample index of bb_[0]
    double t_ = 0.0;                   // read position (absolute samples)
    double t_step_;
    ChannelParams ch_;
    double noise_sigma_ = 0.0;
    double shaped_energy_ = 1.0;
    std::atomic<double> req_esn0_{0.0}, req_cfo_{0.0};
    std::atomic<bool> req_pending_{false};
    double sig_rms_ = 1.0;
    std::size_t n_out_ = 0;
    std::complex<double> lo_{1.0, 0.0}, lo_step_{1.0, 0.0};
    std::mt19937_64 rng_;
    std::normal_distribution<float> gauss_{0.0f, 1.0f};
};

}  // namespace aiw
