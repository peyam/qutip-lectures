#include "receiver.hpp"

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#include "filters.hpp"
#include "frame_decoder.hpp"
#include "front_end.hpp"
#include "snr_estimator.hpp"
#include "sync_correlator.hpp"

namespace aiw {

namespace {

// Blocking (offline source) or dropping (live source) acquisition of a
// producer slot.  Returns nullptr when dropped or stopping.
template <typename T>
T* acquire(SpscRing<T>& ring, bool block, const std::atomic<bool>& stop) {
    Backoff bo;
    for (;;) {
        if (T* s = ring.write_slot()) return s;
        if (!block || stop.load(std::memory_order_relaxed)) return nullptr;
        bo.pause();
    }
}

uint64_t ns_since(Clock::time_point t) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count());
}

}  // namespace

Receiver::Receiver(RxConfig cfg, std::vector<cf32> uw, std::array<uint8_t, PAYLOAD_SIZE> golden)
    : cfg_(std::move(cfg)), uw_(std::move(uw)), golden_(golden) {}

RunSummary Receiver::run(SampleSource& src, const RunOptions& opt) {
    SpscRing<SampleChunk> q_in(128);      // ~750 ms of input at 1.4 MSps
    SpscRing<SampleChunk> q_snr(64);
    SpscRing<SymbolChunk> q_sym(128);
    SpscRing<Frame> q_frames(1024);

    std::atomic<bool> stop{false};
    std::atomic<bool> t1_done{false}, t2_done{false}, t3_done{false}, t4_done{false}, t5_done{false};
    const bool block = !src.is_live();

    // ---------------- Thread 1: radio ingestion ----------------------------
    std::thread t1([&] {
        src.start();
        std::array<cf32, RX_CHUNK_SAMPLES> scratch;
        const bool pace = opt.realtime && !src.is_live();
        const auto t_start = Clock::now();
        uint64_t paced = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (pace) {
                const auto due = t_start + std::chrono::duration_cast<Clock::duration>(
                                               std::chrono::duration<double>(double(paced) / cfg_.sample_rate));
                std::this_thread::sleep_until(due);
            }
            SampleChunk* slot = acquire(q_in, block, stop);
            if (!slot && block) break;  // only happens when stopping
            cf32* dst = slot ? slot->data.data() : scratch.data();
            const std::size_t n = src.read(dst, RX_CHUNK_SAMPLES);
            if (n == 0) break;
            paced += n;
            m_.samples_in.fetch_add(n, std::memory_order_relaxed);
            m_.usrp_overflows.store(src.overflows(), std::memory_order_relaxed);
            if (!slot) {
                m_.chunks_dropped.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            slot->n = n;
            slot->t_ingest = Clock::now();
            q_in.commit_write();
        }
        src.stop();
        t1_done = true;
    });

    // ---------------- Thread 2: front end + timing recovery ----------------
    std::thread t2([&] {
        FrontEnd fe(cfg_);
        std::vector<cf32> dc(RX_CHUNK_SAMPLES), syms;
        syms.reserve(RX_CHUNK_SAMPLES);
        Backoff bo;
        for (;;) {
            SampleChunk* in = q_in.read_slot();
            if (!in) {
                if (t1_done.load()) {
                    if (!(in = q_in.read_slot())) break;
                } else {
                    bo.pause();
                    continue;
                }
            }
            bo.reset();
            syms.clear();
            fe.process(in->data.data(), in->n, dc.data(), syms);
            const auto t_ingest = in->t_ingest;
            const std::size_t n_in = in->n;
            q_in.commit_read();

            // DC-blocked copy to the radiometer: statistics only, so drop
            // rather than stall the data path if T5 falls behind.
            if (SampleChunk* s = acquire(q_snr, false, stop)) {
                std::copy(dc.begin(), dc.begin() + static_cast<std::ptrdiff_t>(n_in), s->data.begin());
                s->n = n_in;
                s->t_ingest = t_ingest;
                q_snr.commit_write();
            } else {
                m_.snr_chunks_dropped.fetch_add(1, std::memory_order_relaxed);
            }

            for (std::size_t off = 0; off < syms.size(); off += SYMBOL_CHUNK_MAX) {
                SymbolChunk* out = acquire(q_sym, block, stop);
                if (!out) {
                    m_.symbol_chunks_dropped.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                const std::size_t n = std::min(SYMBOL_CHUNK_MAX, syms.size() - off);
                std::copy(syms.begin() + static_cast<std::ptrdiff_t>(off),
                          syms.begin() + static_cast<std::ptrdiff_t>(off + n), out->data.begin());
                out->n = n;
                out->t_ingest = t_ingest;
                q_sym.commit_write();
            }
            m_.symbols_out.fetch_add(syms.size(), std::memory_order_relaxed);
            m_.agc_gain.store(fe.agc().gain(), std::memory_order_relaxed);
            m_.pfb_rate.store(fe.pfb().rate(), std::memory_order_relaxed);
        }
        t2_done = true;
    });

    // ---------------- Thread 5: SNR radiometer -----------------------------
    std::thread t5([&] {
        SnrEstimator snr(cfg_.sample_rate, cfg_.snr_avg_len, cfg_.snr_report_block, cfg_.snr_decimation);
        Backoff bo;
        for (;;) {
            SampleChunk* in = q_snr.read_slot();
            if (!in) {
                if (t2_done.load()) {
                    if (!(in = q_snr.read_slot())) break;
                } else {
                    bo.pause();
                    continue;
                }
            }
            bo.reset();
            if (snr.process(in->data.data(), in->n)) m_.snr_db.store(snr.snr_db(), std::memory_order_relaxed);
            q_snr.commit_read();
        }
        t5_done = true;
    });

    // ---------------- Thread 3: frame sync + equalisation + demux ----------
    std::thread t3([&] {
        SyncCorrelator corr(uw_, DATA_LENGTH_SYM, cfg_.corr_threshold);
        TwoPassEqualizer eq(uw_, DATA_LENGTH_SYM, cfg_.eq_delay, cfg_.eq_lambda, cfg_.cfo_correction);
        Backoff bo;
        auto on_segment = [&](Segment&& seg) {
            m_.segments.fetch_add(1, std::memory_order_relaxed);
            m_.corr_metric.store(seg.lead_metric, std::memory_order_relaxed);
            Frame* f = acquire(q_frames, block, stop);
            if (!f) {
                m_.frames_dropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (!eq.process(seg.symbols, f->eq)) {
                m_.eq_failures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            f->corr_metric = seg.lead_metric;
            f->stream_index = seg.stream_index;
            f->t_ingest = seg.t_ingest;
            m_.uw_mer_db.store(f->eq.uw_mer_db, std::memory_order_relaxed);
            m_.cfo_hz.store(f->eq.cfo_rad_per_sym * SYMBOL_RATE / (2.0 * PI), std::memory_order_relaxed);
            m_.eq_cond.store(f->eq.cond_estimate, std::memory_order_relaxed);
            q_frames.commit_write();
        };
        for (;;) {
            SymbolChunk* in = q_sym.read_slot();
            if (!in) {
                if (t2_done.load()) {
                    if (!(in = q_sym.read_slot())) break;
                } else {
                    bo.pause();
                    continue;
                }
            }
            bo.reset();
            corr.push(in->data.data(), in->n, in->t_ingest, on_segment);
            q_sym.commit_read();
            m_.corr_peaks.store(corr.peaks_seen(), std::memory_order_relaxed);
            m_.unconfirmed_peaks.store(corr.unconfirmed_peaks(), std::memory_order_relaxed);
        }
        t3_done = true;
    });

    // ---------------- Thread 4: demod + FEC + BER --------------------------
    std::thread t4([&] {
        FrameDecoder dec(golden_, cfg_.rs_fcr);
        std::vector<Frame> batch;
        batch.reserve(cfg_.fec_batch);
        std::size_t seen = 0;
        Backoff bo;
        auto flush = [&] {
            for (const Frame& f : batch) {
                const DecodeResult r = dec.decode(f.eq.data);
                if (seen++ < cfg_.warmup_frames) {
                    m_.warmup_frames.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                m_.frames_decoded.fetch_add(1, std::memory_order_relaxed);
                m_.pre_fec_bit_errors.fetch_add(r.pre_fec_bit_errors, std::memory_order_relaxed);
                m_.post_fec_bit_errors.fetch_add(r.post_fec_bit_errors, std::memory_order_relaxed);
                if (r.rs_corrected < 0) {
                    m_.frames_uncorrectable.fetch_add(1, std::memory_order_relaxed);
                } else {
                    m_.rs_corrected_symbols.fetch_add(static_cast<uint64_t>(r.rs_corrected), std::memory_order_relaxed);
                    atomic_max(m_.rs_max_corrected, r.rs_corrected);
                }
                if (r.post_fec_bit_errors == 0) m_.frames_payload_ok.fetch_add(1, std::memory_order_relaxed);
                const uint64_t lat = ns_since(f.t_ingest);
                m_.latency_ns_sum.fetch_add(lat, std::memory_order_relaxed);
                atomic_max(m_.latency_ns_max, lat);
            }
            batch.clear();
        };
        for (;;) {
            Frame* f = q_frames.read_slot();
            if (!f) {
                if (t3_done.load()) {
                    if (!(f = q_frames.read_slot())) break;
                } else {
                    bo.pause();
                    continue;
                }
            }
            bo.reset();
            batch.push_back(*f);
            q_frames.commit_read();
            if (batch.size() >= std::max<std::size_t>(1, cfg_.fec_batch)) flush();
        }
        flush();  // partial batch at end of stream
        t4_done = true;
    });

    // ---------------- Metrics dispatcher (calling thread) ------------------
    std::ofstream csv;
    if (!opt.csv_path.empty()) {
        csv.open(opt.csv_path);
        csv << "time_s,samples,frames,frames_ok,uncorrectable,rs_corrected,rs_max_corrected,pre_fec_ber,"
               "post_fec_ber,snr_db,uw_mer_db,cfo_hz,corr_metric,eq_cond,agc_gain,pfb_rate,latency_mean_ms,"
               "latency_max_ms,dropped_chunks,usrp_overflows\n";
    }
    if (!opt.quiet)
        std::cout << "[aiw_rx] source: " << src.describe() << "  UW length " << uw_.size() << ", segment "
                  << (2 * uw_.size() + DATA_LENGTH_SYM) << " symbols\n";

    const auto t0 = Clock::now();
    auto next_report = t0;
    RunSummary sum;
    uint64_t prev_frames = 0, prev_pre = 0, prev_post = 0, prev_ok = 0, prev_unc = 0, prev_corr = 0, prev_lat = 0;
    uint64_t total_lat = 0;
    double max_lat_ms = 0.0;

    auto report = [&](bool final_report) {
        const double t = std::chrono::duration<double>(Clock::now() - t0).count();
        const uint64_t frames = m_.frames_decoded.load();
        const uint64_t pre = m_.pre_fec_bit_errors.load(), post = m_.post_fec_bit_errors.load();
        const uint64_t ok = m_.frames_payload_ok.load(), unc = m_.frames_uncorrectable.load();
        const uint64_t corr = m_.rs_corrected_symbols.load(), lat = m_.latency_ns_sum.load();
        const int rs_max = m_.rs_max_corrected.exchange(0);
        const double lat_max = static_cast<double>(m_.latency_ns_max.exchange(0)) * 1e-6;
        const uint64_t df = frames - prev_frames;
        const double pre_ber = df ? double(pre - prev_pre) / (df * 1728.0) : NAN;
        const double post_ber = df ? double(post - prev_post) / (df * 1600.0) : NAN;
        const double lat_mean = df ? double(lat - prev_lat) * 1e-6 / double(df) : NAN;
        sum.max_rs_corrected = std::max(sum.max_rs_corrected, rs_max);
        max_lat_ms = std::max(max_lat_ms, lat_max);
        const uint64_t dropped = m_.chunks_dropped.load() + m_.symbol_chunks_dropped.load() + m_.frames_dropped.load();

        if (!opt.quiet && (df || final_report)) {
            std::ostringstream os;
            os << std::fixed << std::setprecision(2) << "[" << std::setw(7) << t << " s] frames " << std::setw(4) << df
               << " ok " << std::setw(4) << (ok - prev_ok) << " unc " << (unc - prev_unc) << " | RS fixed "
               << (corr - prev_corr) << " (max " << rs_max << "/blk) | BER pre " << std::scientific
               << std::setprecision(2) << pre_ber << " post " << post_ber << std::fixed << std::setprecision(1)
               << " | SNR " << m_.snr_db.load() << " dB MER " << m_.uw_mer_db.load() << " dB CFO "
               << std::setprecision(0) << m_.cfo_hz.load() << " Hz | lat " << std::setprecision(1) << lat_mean
               << "/" << lat_max << " ms";
            if (dropped) os << " | DROPPED " << dropped;
            if (m_.usrp_overflows.load()) os << " | O " << m_.usrp_overflows.load();
            std::cout << os.str() << "\n" << std::flush;
        }
        if (csv.is_open() && df) {
            csv << std::setprecision(6) << t << ',' << m_.samples_in.load() << ',' << df << ',' << (ok - prev_ok) << ','
                << (unc - prev_unc) << ',' << (corr - prev_corr) << ',' << rs_max << ',' << pre_ber << ',' << post_ber
                << ',' << m_.snr_db.load() << ',' << m_.uw_mer_db.load() << ',' << m_.cfo_hz.load() << ','
                << m_.corr_metric.load() << ',' << m_.eq_cond.load() << ',' << m_.agc_gain.load() << ','
                << m_.pfb_rate.load() << ',' << lat_mean << ',' << lat_max << ',' << dropped << ','
                << m_.usrp_overflows.load() << '\n'
                << std::flush;
        }
        prev_frames = frames;
        prev_pre = pre;
        prev_post = post;
        prev_ok = ok;
        prev_unc = unc;
        prev_corr = corr;
        prev_lat = lat;
        total_lat = lat;
    };

    const auto interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(opt.report_interval_s));
    while (!t4_done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto now = Clock::now();
        if (now >= next_report + interval) {
            next_report = now;
            report(false);
        }
        const bool timed_out = opt.duration_s > 0 && std::chrono::duration<double>(now - t0).count() >= opt.duration_s;
        const bool ext = opt.external_stop && opt.external_stop->load();
        if ((timed_out || ext) && !stop.load()) stop = true;
    }
    stop = true;
    t1.join();
    t2.join();
    t5.join();
    t3.join();
    t4.join();
    report(true);

    sum.samples = m_.samples_in.load();
    sum.frames = m_.frames_decoded.load();
    sum.warmup_frames = m_.warmup_frames.load();
    sum.uncorrectable = m_.frames_uncorrectable.load();
    sum.payload_ok = m_.frames_payload_ok.load();
    sum.pre_fec_bit_errors = m_.pre_fec_bit_errors.load();
    sum.post_fec_bit_errors = m_.post_fec_bit_errors.load();
    sum.dropped_chunks = m_.chunks_dropped.load() + m_.symbol_chunks_dropped.load() + m_.frames_dropped.load();
    sum.overflows = m_.usrp_overflows.load();
    sum.mean_latency_ms = sum.frames ? double(total_lat) * 1e-6 / double(sum.frames) : NAN;
    sum.max_latency_ms = max_lat_ms;
    sum.last_snr_db = m_.snr_db.load();
    sum.last_mer_db = m_.uw_mer_db.load();
    return sum;
}

}  // namespace aiw
