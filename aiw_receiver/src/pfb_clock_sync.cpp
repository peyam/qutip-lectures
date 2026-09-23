#include "pfb_clock_sync.hpp"

#include <algorithm>
#include <cmath>

#include "filters.hpp"

namespace aiw {

namespace {

// GNU Radio's create_diff_taps: [-1 0 1] differentiator, normalised so the
// sum of |taps| equals nfilts.
std::vector<float> make_diff_taps(const std::vector<float>& taps, int nfilts) {
    std::vector<float> d;
    d.reserve(taps.size());
    d.push_back(0.0f);
    float pwr = 0.0f;
    for (std::size_t i = 0; i + 2 < taps.size(); ++i) {
        const float t = -taps[i] + taps[i + 2];
        d.push_back(t);
        pwr += std::fabs(t);
    }
    d.push_back(0.0f);
    if (pwr != 0.0f)
        for (auto& t : d) t *= static_cast<float>(nfilts) / pwr;
    return d;
}

std::vector<std::vector<float>> split_bank(const std::vector<float>& taps, int nfilts, std::size_t tpf) {
    std::vector<std::vector<float>> bank(static_cast<std::size_t>(nfilts), std::vector<float>(tpf, 0.0f));
    for (int i = 0; i < nfilts; ++i) {
        for (std::size_t j = 0; j < tpf; ++j) {
            const std::size_t src = static_cast<std::size_t>(i) + j * static_cast<std::size_t>(nfilts);
            if (src < taps.size()) bank[static_cast<std::size_t>(i)][j] = taps[src];
        }
        std::reverse(bank[static_cast<std::size_t>(i)].begin(), bank[static_cast<std::size_t>(i)].end());
    }
    return bank;
}

}  // namespace

PfbClockSync::PfbClockSync(int sps, double loop_bw, double damping, int nfilts, double alpha,
                           int nsym_span, double max_rate_dev)
    : sps_(sps), nfilts_(nfilts), max_dev_(max_rate_dev) {
    // Prototype: gain nfilts so each branch has ~unity DC gain.
    prototype_ = firdes::root_raised_cosine(nfilts, static_cast<double>(nfilts) * sps, 1.0, alpha,
                                            nsym_span * sps * nfilts);
    taps_per_filter_ = (prototype_.size() + static_cast<std::size_t>(nfilts) - 1) / static_cast<std::size_t>(nfilts);
    filters_ = split_bank(prototype_, nfilts, taps_per_filter_);
    dfilters_ = split_bank(make_diff_taps(prototype_, nfilts), nfilts, taps_per_filter_);

    const double denom = 1.0 + 2.0 * damping * loop_bw + loop_bw * loop_bw;
    alpha_gain_ = (4.0 * damping * loop_bw) / denom;
    beta_gain_ = (4.0 * loop_bw * loop_bw) / denom;

    k_ = nfilts / 2.0;  // GNU Radio default initial phase
    // Leading zeros give the filter history and room for a negative wrap.
    buf_.assign(taps_per_filter_ + 2, cf32{});
    count_ = 2;
}

cf32 PfbClockSync::filter(const std::vector<float>& taps_rev, std::size_t idx) const {
    const float* x = reinterpret_cast<const float*>(&buf_[idx]);
    float re = 0.0f, im = 0.0f;
    const std::size_t n = taps_rev.size();
    for (std::size_t k = 0; k < n; ++k) {
        re += taps_rev[k] * x[2 * k];
        im += taps_rev[k] * x[2 * k + 1];
    }
    return {re, im};
}

void PfbClockSync::process(const cf32* in, std::size_t n, std::vector<cf32>& out) {
    buf_.insert(buf_.end(), in, in + n);
    const long need = static_cast<long>(taps_per_filter_) + sps_ + 1;

    while (count_ + need < static_cast<long>(buf_.size())) {
        int filtnum = static_cast<int>(std::floor(k_));
        while (filtnum >= nfilts_) {
            k_ -= nfilts_;
            filtnum -= nfilts_;
            ++count_;
        }
        while (filtnum < 0) {
            k_ += nfilts_;
            filtnum += nfilts_;
            --count_;
        }
        if (count_ < 0) count_ = 0;  // only reachable in pathological start-up

        const auto idx = static_cast<std::size_t>(count_);
        const cf32 y = filter(filters_[static_cast<std::size_t>(filtnum)], idx);
        const cf32 dy = filter(dfilters_[static_cast<std::size_t>(filtnum)], idx);
        out.push_back(y);

        k_ += rate_f_;  // integer part of (sps - floor(sps)) * nfilts is 0 here

        // Timing error: Re(y)Re(dy) + Im(y)Im(dy), averaged over I/Q.
        error_ = 0.5 * (static_cast<double>(y.real()) * dy.real() + static_cast<double>(y.imag()) * dy.imag());
        for (int s = 0; s < sps_; ++s) {
            rate_f_ += beta_gain_ * error_;
            k_ += rate_f_ + alpha_gain_ * error_;
        }
        rate_f_ = std::clamp(rate_f_, -max_dev_, max_dev_);
        count_ += sps_;
    }

    // Drop consumed samples, keeping a little history for negative wraps.
    const long keep_before = 2;
    if (count_ > keep_before) {
        const long drop = count_ - keep_before;
        buf_.erase(buf_.begin(), buf_.begin() + drop);
        count_ -= drop;
    }
}

}  // namespace aiw
