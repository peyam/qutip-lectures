#include "tx_simulator.hpp"

#include <cmath>

#include "constellation.hpp"
#include "fec_reed_solomon.hpp"
#include "filters.hpp"

namespace aiw {

TxSimulator::TxSimulator(std::vector<cf32> uw, std::array<uint8_t, 200> payload, ChannelParams ch, int rs_fcr)
    : ch_(ch), rng_(ch.seed) {
    ReedSolomon rs(rs_fcr);
    rs.encode_shortened(payload, codeword_);

    frame_.insert(frame_.end(), uw.begin(), uw.end());
    for (uint8_t b : codeword_) frame_.push_back(qam256::map(b));
    frame_.insert(frame_.end(), uw.begin(), uw.end());

    rrc_ = firdes::root_raised_cosine(1.0, SPS, 1.0, RRC_ALPHA, NSYM_FILT * SPS);
    sym_hist_.assign(static_cast<std::size_t>(NSYM_FILT + 1), cf32{});

    // RMS of the shaped signal for unit-power symbols at SPS samples/symbol.
    double e = 0.0;
    for (float h : rrc_) e += static_cast<double>(h) * h;
    sig_rms_ = std::sqrt(e / SPS);
    // Es/N0 referenced to the matched-filter output: per-sample complex noise
    // variance = Es / (N0-normalised) with Es = sum h^2.
    shaped_energy_ = e;

    t_step_ = 1.0 + ch_.clock_ppm * 1e-6;
    t_ = 8.0 + ch_.timing_offset;
    req_esn0_.store(ch_.esn0_db);
    req_cfo_.store(ch_.cfo_hz);
    apply_channel();
}

void TxSimulator::apply_channel() {
    ch_.esn0_db = req_esn0_.load();
    ch_.cfo_hz = req_cfo_.load();
    noise_sigma_ = ch_.esn0_db < 200.0
                       ? std::sqrt(shaped_energy_ / std::pow(10.0, ch_.esn0_db / 10.0) / 2.0)
                       : 0.0;
    lo_step_ = std::polar(1.0, 2.0 * PI * (ch_.if_offset_hz + ch_.cfo_hz) / SAMPLE_RATE);
}

void TxSimulator::refill_baseband() {
    // Pulse shaping by explicit polyphase upsampling of the symbol stream.
    for (const cf32& s : frame_) {
        sym_hist_.pop_front();
        sym_hist_.push_back(s);
        // Output SPS samples: y[m] = sum_j sym[j] h[m - j*SPS]
        for (int ph = 0; ph < SPS; ++ph) {
            cf32 acc{};
            const std::size_t ns = sym_hist_.size();
            for (std::size_t j = 0; j < ns; ++j) {
                const long tap = static_cast<long>((ns - 1 - j) * SPS) + ph;
                if (tap >= 0 && tap < static_cast<long>(rrc_.size())) acc += sym_hist_[j] * rrc_[static_cast<std::size_t>(tap)];
            }
            bb_.push_back(acc);
        }
    }
}

cf32 TxSimulator::interp(double t) {
    const double rel = t - bb_base_;
    const auto i = static_cast<long>(std::floor(rel));
    const double mu = rel - static_cast<double>(i);
    auto at = [&](long k) -> cf32 {
        return (k >= 0 && k < static_cast<long>(bb_.size())) ? bb_[static_cast<std::size_t>(k)] : cf32{};
    };
    const cf32 y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const float m = static_cast<float>(mu);
    // Cubic Lagrange on points -1, 0, 1, 2.
    const float c0 = -m * (m - 1.0f) * (m - 2.0f) / 6.0f;
    const float c1 = (m + 1.0f) * (m - 1.0f) * (m - 2.0f) / 2.0f;
    const float c2 = -(m + 1.0f) * m * (m - 2.0f) / 2.0f;
    const float c3 = (m + 1.0f) * m * (m - 1.0f) / 6.0f;
    return y0 * c0 + y1 * c1 + y2 * c2 + y3 * c3;
}

void TxSimulator::generate(cf32* out, std::size_t n) {
    if (req_pending_.exchange(false)) apply_channel();
    const float amp = ch_.amplitude / static_cast<float>(sig_rms_);
    for (std::size_t k = 0; k < n; ++k) {
        while (t_ + 4.0 >= bb_base_ + static_cast<double>(bb_.size())) refill_baseband();
        cf32 s = interp(t_);
        const cf32 s_echo = interp(t_ - SPS);
        s += ch_.echo * s_echo;
        if (noise_sigma_ > 0.0)
            s += cf32(gauss_(rng_), gauss_(rng_)) * static_cast<float>(noise_sigma_);
        const cf32 lo(static_cast<float>(lo_.real()), static_cast<float>(lo_.imag()));
        out[k] = (s * lo) * amp + ch_.dc_offset * ch_.amplitude;
        lo_ *= lo_step_;
        if ((++n_out_ & 1023u) == 0) lo_ /= std::abs(lo_);
        t_ += t_step_;

        // Discard baseband no longer reachable by the interpolator.
        const double keep_from = t_ - 2.0 * SPS - 4.0;
        if (keep_from - bb_base_ > 65536.0) {
            const auto drop = static_cast<std::size_t>(keep_from - bb_base_);
            bb_.erase(bb_.begin(), bb_.begin() + static_cast<std::ptrdiff_t>(drop));
            bb_base_ += static_cast<double>(drop);
        }
    }
}

}  // namespace aiw
