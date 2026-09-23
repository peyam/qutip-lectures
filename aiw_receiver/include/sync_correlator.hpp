// Unique-word correlator and segment extractor (spec 5.2).
//
// Streams 1-sps symbols, computes the sliding correlation with the UW,
//     C[n] = sum_k s[n+k] u*[k],
// and a normalised metric m[n] = |C[n]|^2 / (L * E[n]) where E[n] is the
// energy of the L-symbol window (m = 1 for a perfect, noise-free UW
// regardless of gain).  A segment starts at a local peak n0 above the
// threshold that is confirmed by a trailing-UW peak at n0 + L + 216.
//
// Each emitted segment carries SEGMENT_MARGIN symbols of context on both
// sides for the equalizer.
#pragma once

#include <chrono>
#include <complex>
#include <cstddef>
#include <functional>
#include <vector>

#include "config.hpp"

namespace aiw {

struct Segment {
    std::vector<cf32> symbols;          // margin + (2L + 216) + margin
    float lead_metric = 0.0f;
    float trail_metric = 0.0f;
    std::size_t stream_index = 0;       // absolute symbol index of the leading UW
    std::chrono::steady_clock::time_point t_ingest{};
};

class SyncCorrelator {
public:
    SyncCorrelator(std::vector<cf32> uw, std::size_t data_len, float threshold);

    std::size_t uw_len() const { return uw_.size(); }
    std::size_t segment_len() const { return 2 * uw_.size() + data_len_; }

    // Appends symbols; invokes `on_segment` for every confirmed segment.
    void push(const cf32* sym, std::size_t n, std::chrono::steady_clock::time_point t,
              const std::function<void(Segment&&)>& on_segment);

    // Normalised correlation metric at absolute buffer offset (exposed for tests).
    float metric_at(std::size_t buf_pos) const;

    std::size_t peaks_seen() const { return peaks_seen_; }
    std::size_t unconfirmed_peaks() const { return unconfirmed_; }

private:
    std::vector<cf32> uw_conj_;
    std::vector<cf32> uw_;
    std::size_t data_len_;
    float threshold_;

    std::vector<cf32> buf_;
    std::size_t buf_base_ = 0;   // absolute index of buf_[0]
    std::size_t scan_ = SEGMENT_MARGIN;  // next buffer offset to test
    std::size_t peaks_seen_ = 0;
    std::size_t unconfirmed_ = 0;
};

}  // namespace aiw
