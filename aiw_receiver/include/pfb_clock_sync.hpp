// Polyphase filterbank clock synchroniser (spec 4.4).
//
// A port of GNU Radio's pfb_clock_sync_ccf: a 32-branch filterbank of the
// RRC matched filter plus a matching bank of derivative filters drives a
// second-order loop that selects the branch (fractional delay) maximising
// the matched-filter output.  Output rate is one sample per symbol.
//
// The filterbank *is* the matched filter: the prototype is the RRC
// (alpha 0.35, 16-symbol span) at N_FILTS x SPS samples per symbol, i.e.
// 32 branches x 64 taps.  Running a separate 64-tap RRC in front of it would
// apply the matched filter twice (a raised-cosine-squared pulse with ISI), so
// stage 4 and stage 5 of the thread-2 diagram are one block here.
#pragma once

#include <vector>

#include "config.hpp"

namespace aiw {

class PfbClockSync {
public:
    PfbClockSync(int sps, double loop_bw, double damping, int nfilts, double alpha, int nsym_span,
                 double max_rate_dev = 1.5);

    // Consumes all `n` input samples (4 sps) and appends the recovered
    // symbols (1 sps) to `out`.
    void process(const cf32* in, std::size_t n, std::vector<cf32>& out);

    // Diagnostics.
    double rate() const { return rate_f_; }
    double phase() const { return k_; }
    double error() const { return error_; }
    std::size_t taps_per_filter() const { return taps_per_filter_; }
    const std::vector<float>& prototype() const { return prototype_; }

private:
    cf32 filter(const std::vector<float>& taps_rev, std::size_t idx) const;

    int sps_;
    int nfilts_;
    double alpha_gain_ = 0.0;
    double beta_gain_ = 0.0;
    double max_dev_;

    std::vector<float> prototype_;
    std::size_t taps_per_filter_ = 0;
    std::vector<std::vector<float>> filters_;   // reversed taps per branch
    std::vector<std::vector<float>> dfilters_;  // reversed derivative taps

    std::vector<cf32> buf_;
    long count_ = 0;    // index of the oldest sample of the next filter window
    double k_ = 0.0;    // fractional branch position
    double rate_f_ = 0.0;
    double error_ = 0.0;
};

}  // namespace aiw
