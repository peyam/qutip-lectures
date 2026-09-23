// Multi-threaded AIW-Rx pipeline (spec 3).
//
//   T1 radio ingestion  --SampleChunk-->  T2 front end + timing  --SymbolChunk-->
//   T3 frame sync / EQ  --Frame-->        T4 slicer / RS / BER
//   T2 also forwards the DC-blocked stream to T5 (SNR radiometer).
// The calling thread runs the periodic metrics dispatcher.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "circular_buffer.hpp"
#include "config.hpp"
#include "equalizer.hpp"
#include "metrics.hpp"
#include "sample_source.hpp"

namespace aiw {

using Clock = std::chrono::steady_clock;

struct SampleChunk {
    std::array<cf32, RX_CHUNK_SAMPLES> data;
    std::size_t n = 0;
    Clock::time_point t_ingest{};
};

struct SymbolChunk {
    std::array<cf32, SYMBOL_CHUNK_MAX> data;
    std::size_t n = 0;
    Clock::time_point t_ingest{};
};

struct Frame {
    EqualizerResult eq;
    float corr_metric = 0.0f;
    std::size_t stream_index = 0;
    Clock::time_point t_ingest{};
};

struct RunOptions {
    double duration_s = 0.0;       // 0 = until the source ends / SIGINT
    double report_interval_s = 1.0;
    std::string csv_path;
    bool quiet = false;
    bool realtime = false;         // pace offline sources at the sample rate
    const std::atomic<bool>* external_stop = nullptr;
};

struct RunSummary {
    uint64_t samples = 0;
    uint64_t frames = 0;         // excluding warm-up
    uint64_t warmup_frames = 0;
    uint64_t uncorrectable = 0;
    uint64_t payload_ok = 0;
    uint64_t pre_fec_bit_errors = 0;
    uint64_t post_fec_bit_errors = 0;
    uint64_t dropped_chunks = 0;
    uint64_t overflows = 0;
    int max_rs_corrected = 0;
    double mean_latency_ms = 0.0;
    double max_latency_ms = 0.0;
    double last_snr_db = NAN;
    double last_mer_db = NAN;
    double pre_fec_ber() const { return frames ? double(pre_fec_bit_errors) / (frames * 1728.0) : NAN; }
    double post_fec_ber() const { return frames ? double(post_fec_bit_errors) / (frames * 1600.0) : NAN; }
};

class Receiver {
public:
    Receiver(RxConfig cfg, std::vector<cf32> uw, std::array<uint8_t, PAYLOAD_SIZE> golden);
    RunSummary run(SampleSource& src, const RunOptions& opt);

private:
    RxConfig cfg_;
    std::vector<cf32> uw_;
    std::array<uint8_t, PAYLOAD_SIZE> golden_;
    Metrics m_;
};

}  // namespace aiw
