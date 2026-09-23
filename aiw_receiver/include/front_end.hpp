// Thread-2 signal chain (spec 4): DC blocker -> +300 kHz translation + FIR
// -> AGC -> RRC polyphase clock sync (matched filter, 4 sps -> 1 sps).
#pragma once

#include <vector>

#include "agc.hpp"
#include "config.hpp"
#include "dc_blocker.hpp"
#include "filters.hpp"
#include "pfb_clock_sync.hpp"

namespace aiw {

class FrontEnd {
public:
    explicit FrontEnd(const RxConfig& c)
        : dc_(c.dc_blocker_len),
          xlat_(c.xlat_offset_hz, c.sample_rate, OCCUPIED_BW),
          agc_(c.agc_attack, c.agc_decay, c.agc_reference, c.agc_max_gain),
          pfb_(SPS, c.pfb_loop_bw, c.pfb_damping, N_FILTS, RRC_ALPHA, NSYM_FILT, c.pfb_max_dev) {}

    // `dc_out` (n samples) receives the DC-blocked, un-translated stream for
    // the SNR radiometer; recovered symbols are appended to `symbols`.
    void process(const cf32* in, std::size_t n, cf32* dc_out, std::vector<cf32>& symbols) {
        dc_.process(in, dc_out, n);
        work_.resize(n);
        xlat_.process(dc_out, work_.data(), n);
        agc_.process(work_.data(), work_.data(), n);
        pfb_.process(work_.data(), n, symbols);
    }

    const Agc& agc() const { return agc_; }
    const PfbClockSync& pfb() const { return pfb_; }
    std::size_t xlat_taps() const { return xlat_.ntaps(); }

private:
    DcBlocker dc_;
    FreqXlatingFir xlat_;
    Agc agc_;
    PfbClockSync pfb_;
    std::vector<cf32> work_;
};

}  // namespace aiw
