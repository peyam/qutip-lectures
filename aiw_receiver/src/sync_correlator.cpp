#include "sync_correlator.hpp"

#include <algorithm>

namespace aiw {

SyncCorrelator::SyncCorrelator(std::vector<cf32> uw, std::size_t data_len, float threshold)
    : uw_(std::move(uw)), data_len_(data_len), threshold_(threshold) {
    uw_conj_.resize(uw_.size());
    std::transform(uw_.begin(), uw_.end(), uw_conj_.begin(), [](cf32 v) { return std::conj(v); });
}

float SyncCorrelator::metric_at(std::size_t p) const {
    const std::size_t L = uw_.size();
    const float* x = reinterpret_cast<const float*>(&buf_[p]);
    const float* u = reinterpret_cast<const float*>(uw_conj_.data());
    float cr = 0.0f, ci = 0.0f, e = 0.0f, eu = 0.0f;
    for (std::size_t k = 0; k < L; ++k) {
        const float xr = x[2 * k], xi = x[2 * k + 1];
        const float ur = u[2 * k], ui = u[2 * k + 1];
        cr += xr * ur - xi * ui;
        ci += xr * ui + xi * ur;
        e += xr * xr + xi * xi;
        eu += ur * ur + ui * ui;
    }
    const float den = e * eu;
    return den > 0.0f ? (cr * cr + ci * ci) / den : 0.0f;
}

void SyncCorrelator::push(const cf32* sym, std::size_t n, std::chrono::steady_clock::time_point t,
                          const std::function<void(Segment&&)>& on_segment) {
    buf_.insert(buf_.end(), sym, sym + n);
    const std::size_t L = uw_.size();
    const std::size_t trail_off = L + data_len_;
    const std::size_t seg_len = segment_len();
    constexpr std::size_t PEAK_WIN = 8;   // local-maximum search window
    constexpr std::size_t TRAIL_SLACK = 1;  // tolerate +-1 symbol timing slip

    // A candidate at p needs [p - margin, p + seg_len + margin) plus the
    // peak-search window to be buffered.
    while (scan_ + PEAK_WIN + seg_len + SEGMENT_MARGIN + TRAIL_SLACK <= buf_.size()) {
        if (metric_at(scan_) <= threshold_) {
            ++scan_;
            continue;
        }
        // Refine to the local maximum.
        std::size_t p = scan_;
        float best = metric_at(p);
        for (std::size_t q = scan_ + 1; q < scan_ + PEAK_WIN; ++q) {
            const float m = metric_at(q);
            if (m > best) {
                best = m;
                p = q;
            }
        }
        ++peaks_seen_;

        float trail = 0.0f;
        for (std::size_t q = p + trail_off - TRAIL_SLACK; q <= p + trail_off + TRAIL_SLACK; ++q)
            trail = std::max(trail, metric_at(q));

        if (trail > threshold_ && p >= SEGMENT_MARGIN) {
            Segment seg;
            seg.symbols.assign(buf_.begin() + static_cast<std::ptrdiff_t>(p - SEGMENT_MARGIN),
                               buf_.begin() + static_cast<std::ptrdiff_t>(p + seg_len + SEGMENT_MARGIN));
            seg.lead_metric = best;
            seg.trail_metric = trail;
            seg.stream_index = buf_base_ + p;
            seg.t_ingest = t;
            on_segment(std::move(seg));
            // Next leading UW can start right after this trailing UW; back
            // off slightly to tolerate a slip of the symbol clock.
            scan_ = p + seg_len - 2;
        } else {
            ++unconfirmed_;
            scan_ = p + 1;
        }
    }

    // Trim consumed history (keep margin before scan_).
    if (scan_ > 4096 + SEGMENT_MARGIN) {
        const std::size_t drop = scan_ - SEGMENT_MARGIN;
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(drop));
        buf_base_ += drop;
        scan_ -= drop;
    }
}

}  // namespace aiw
