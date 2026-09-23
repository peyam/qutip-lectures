// Shared counters written by the pipeline threads and read by the metrics
// dispatcher.  Counters are monotonic; the dispatcher reports deltas.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

namespace aiw {

template <typename T>
inline void atomic_max(std::atomic<T>& a, T v) {
    T cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}

struct Metrics {
    // Thread 1
    std::atomic<uint64_t> samples_in{0};
    std::atomic<uint64_t> chunks_dropped{0};
    std::atomic<uint64_t> usrp_overflows{0};
    // Thread 2
    std::atomic<uint64_t> symbols_out{0};
    std::atomic<uint64_t> symbol_chunks_dropped{0};
    std::atomic<uint64_t> snr_chunks_dropped{0};
    std::atomic<double> agc_gain{0.0};
    std::atomic<double> pfb_rate{0.0};
    // Thread 3
    std::atomic<uint64_t> corr_peaks{0};
    std::atomic<uint64_t> unconfirmed_peaks{0};
    std::atomic<uint64_t> segments{0};
    std::atomic<uint64_t> eq_failures{0};
    std::atomic<uint64_t> frames_dropped{0};
    std::atomic<double> corr_metric{NAN};
    std::atomic<double> uw_mer_db{NAN};
    std::atomic<double> cfo_hz{NAN};
    std::atomic<double> eq_cond{NAN};
    // Thread 4
    std::atomic<uint64_t> warmup_frames{0};
    std::atomic<uint64_t> frames_decoded{0};
    std::atomic<uint64_t> frames_uncorrectable{0};
    std::atomic<uint64_t> frames_payload_ok{0};
    std::atomic<uint64_t> rs_corrected_symbols{0};
    std::atomic<int> rs_max_corrected{0};
    std::atomic<uint64_t> pre_fec_bit_errors{0};
    std::atomic<uint64_t> post_fec_bit_errors{0};
    std::atomic<uint64_t> latency_ns_sum{0};
    std::atomic<uint64_t> latency_ns_max{0};
    // Thread 5
    std::atomic<double> snr_db{NAN};
};

}  // namespace aiw
