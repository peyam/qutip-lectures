// Offline verification of the AIW-Rx DSP chain (spec section 9 gates that do
// not need hardware).  Plain asserts, no framework dependency.

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "agc.hpp"
#include "circular_buffer.hpp"
#include "constellation.hpp"
#include "dashboard.hpp"
#include "sample_source.hpp"
#include "spectrum.hpp"
#include "dc_blocker.hpp"
#include "equalizer.hpp"
#include "fec_reed_solomon.hpp"
#include "filters.hpp"
#include "frame_decoder.hpp"
#include "front_end.hpp"
#include "golden_payload.hpp"
#include "snr_estimator.hpp"
#include "sync_correlator.hpp"
#include "tx_simulator.hpp"
#include "unique_word.hpp"

using namespace aiw;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(cond)) {                                                               \
            ++g_failures;                                                            \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                            \
    } while (0)

void run(const char* name, const std::function<void()>& fn) {
    const int before = g_failures;
    std::printf("[ RUN  ] %s\n", name);
    fn();
    std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", name);
}

// ---------------------------------------------------------------------------

void test_unique_word() {
    const auto zc = make_zadoff_chu(143, 25);
    CHECK(zc.size() == UNIQUE_WORD_SPEC_TABLE.size());
    float max_err = 0.0f;
    for (std::size_t i = 0; i < zc.size(); ++i) max_err = std::max(max_err, std::abs(zc[i] - UNIQUE_WORD_SPEC_TABLE[i]));
    std::printf("  spec UW table: %zu symbols, max |table - ZC(143,25)| = %.2e\n", zc.size(), max_err);
    CHECK(max_err < 1e-5f);
    for (std::size_t i = 0; i < zc.size(); ++i) CHECK(std::abs(std::abs(zc[i]) - 1.0f) < 1e-5f);
    CHECK(make_zadoff_chu(136, 25).size() == 136);
}

void test_golden_payload() {
    // First bytes of np.random.default_rng(42).integers(0, 256, 200, dtype=np.uint8)
    CHECK(GOLDEN_PAYLOAD[0] == 0x88 && GOLDEN_PAYLOAD[1] == 0x26 && GOLDEN_PAYLOAD[2] == 0xd9);
    CHECK(GOLDEN_PAYLOAD[198] == 0x44 && GOLDEN_PAYLOAD[199] == 0xc7);
}

void test_qam256() {
    CHECK(std::abs(qam256::SCALE - 13.0384048f) < 1e-5f);
    double p = 0.0;
    for (int b = 0; b < 256; ++b) {
        const cf32 s = qam256::map(static_cast<uint8_t>(b));
        p += std::norm(s);
        CHECK(qam256::slice(s) == b);
        // Any perturbation strictly inside the decision region.
        const float d = 0.99f / qam256::SCALE;
        CHECK(qam256::slice(s + cf32(d, -d)) == b || (b & 0x0F) == 0 || (b >> 4) == 15);
    }
    CHECK(std::abs(p / 256.0 - 1.0) < 1e-5);
    // Nearest-odd rounding (the spec's printed formula gets these wrong).
    CHECK(qam256::slice_level(0.5f) == 1);
    CHECK(qam256::slice_level(-0.5f) == -1);
    CHECK(qam256::slice_level(2.5f) == 3);
    CHECK(qam256::slice_level(1.9f) == 1);
    CHECK(qam256::slice_level(2.1f) == 3);
    CHECK(qam256::slice_level(40.0f) == 15);
    CHECK(qam256::slice_level(-40.0f) == -15);
}

void test_reed_solomon() {
    for (int fcr : {0, 1}) {
        ReedSolomon rs(fcr);
        std::mt19937 rng(7 + fcr);
        int corrected_ok = 0, detected_fail = 0, trials_fail = 0;
        for (int trial = 0; trial < 300; ++trial) {
            std::array<uint8_t, 200> msg;
            for (auto& b : msg) b = static_cast<uint8_t>(rng());
            std::array<uint8_t, 216> cw;
            rs.encode_shortened(msg, cw);
            // Clean codeword.
            auto clean = cw;
            CHECK(rs.decode_shortened(clean) == 0);

            const int nerr = trial % 10;  // 0..9 errors
            auto bad = cw;
            std::vector<int> pos(216);
            for (int i = 0; i < 216; ++i) pos[static_cast<std::size_t>(i)] = i;
            std::shuffle(pos.begin(), pos.end(), rng);
            for (int e = 0; e < nerr; ++e)
                bad[static_cast<std::size_t>(pos[static_cast<std::size_t>(e)])] ^= static_cast<uint8_t>(1 + rng() % 255);
            const int r = rs.decode_shortened(bad);
            if (nerr <= 8) {
                CHECK(r == nerr);
                CHECK(bad == cw);
                if (r == nerr && bad == cw) ++corrected_ok;
            } else {
                ++trials_fail;
                if (r < 0) ++detected_fail;
            }
        }
        std::printf("  fcr=%d: %d/270 blocks with <=8 errors corrected, %d/%d 9-error blocks flagged uncorrectable\n",
                    fcr, corrected_ok, detected_fail, trials_fail);
        CHECK(detected_fail >= trials_fail * 9 / 10);
    }
}

void test_reed_solomon_interop() {
    // Parity of GOLDEN_PAYLOAD from Python reedsolo:
    //   reedsolo.RSCodec(16, nsize=255, fcr=0, prim=0x11d, generator=2)
    const uint8_t expected[16] = {0x84, 0x9c, 0xfc, 0x14, 0x20, 0xb7, 0x95, 0x9f,
                                  0x33, 0x81, 0x73, 0x25, 0xfb, 0xc4, 0x4c, 0x59};
    ReedSolomon rs(0);
    std::array<uint8_t, 216> cw;
    rs.encode_shortened(GOLDEN_PAYLOAD, cw);
    for (int i = 0; i < 16; ++i) CHECK(cw[static_cast<std::size_t>(200 + i)] == expected[i]);
}

void test_ring_buffer() {
    SpscRing<uint64_t> q(1024);
    constexpr uint64_t N = 2'000'000;
    std::thread prod([&] {
        for (uint64_t i = 0; i < N;) {
            if (q.try_push(i)) ++i;
        }
    });
    uint64_t expect = 0;
    bool ordered = true;
    while (expect < N) {
        uint64_t v;
        if (q.try_pop(v)) {
            ordered &= (v == expect);
            ++expect;
        }
    }
    prod.join();
    CHECK(ordered);
}

void test_dc_blocker_and_agc() {
    DcBlocker dc(32);
    CHECK(std::abs(dc.alpha() - 0.96875f) < 1e-7f);
    std::vector<cf32> x(20000, cf32(0.3f, -0.2f)), y(x.size());
    dc.process(x.data(), y.data(), x.size());
    CHECK(std::abs(y.back()) < 1e-4f);

    Agc agc(0.1f, 0.001f, 1.0f, 65536.0f);
    std::vector<cf32> s(50000), o(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) s[i] = std::polar(0.01f, 0.1f * static_cast<float>(i));
    agc.process(s.data(), o.data(), s.size());
    CHECK(std::abs(std::abs(o.back()) - 1.0f) < 0.02f);
}

void test_xlating_filter() {
    // A tone at +300 kHz must come out at DC; a tone at -400 kHz is rejected.
    FreqXlatingFir x(300e3, SAMPLE_RATE, OCCUPIED_BW);
    const std::size_t n = 20000;
    std::vector<cf32> a(n), b(n), ya(n), yb(n);
    for (std::size_t i = 0; i < n; ++i) {
        a[i] = std::polar(1.0f, static_cast<float>(2 * PI * 300e3 / SAMPLE_RATE * static_cast<double>(i)));
        b[i] = std::polar(1.0f, static_cast<float>(2 * PI * -400e3 / SAMPLE_RATE * static_cast<double>(i)));
    }
    x.process(a.data(), ya.data(), n);
    FreqXlatingFir x2(300e3, SAMPLE_RATE, OCCUPIED_BW);
    x2.process(b.data(), yb.data(), n);
    // Output at DC: consecutive samples have (nearly) the same phase.
    CHECK(std::abs(std::arg(ya[n - 1] * std::conj(ya[n - 2]))) < 1e-3f);
    CHECK(std::abs(std::abs(ya[n - 1]) - 1.0f) < 0.01f);
    const float rej_db = 20.0f * std::log10(std::abs(yb[n - 1]) + 1e-12f);
    std::printf("  translation FIR: %zu taps, -700 kHz image rejection %.1f dB\n", x.ntaps(), rej_db);
    CHECK(rej_db < -40.0f);
}

void test_equalizer_synthetic() {
    // Symbol-level: known segment through a 3-tap channel, phase and CFO.
    const auto uw = default_unique_word();
    std::mt19937 rng(3);
    std::array<uint8_t, 216> bytes;
    for (auto& b : bytes) b = static_cast<uint8_t>(rng());
    std::vector<cf32> tx;
    for (int i = 0; i < 4; ++i) tx.push_back(qam256::map(static_cast<uint8_t>(rng())));
    tx.insert(tx.end(), uw.begin(), uw.end());
    for (auto b : bytes) tx.push_back(qam256::map(b));
    tx.insert(tx.end(), uw.begin(), uw.end());
    for (int i = 0; i < 4; ++i) tx.push_back(qam256::map(static_cast<uint8_t>(rng())));

    const cf32 h[3] = {{0.1f, 0.05f}, {0.9f, 0.3f}, {-0.12f, 0.08f}};  // pre-cursor, main, post-cursor
    const double w = 2 * PI * 200.0 / SYMBOL_RATE;
    std::vector<cf32> rx(tx.size());
    for (std::size_t n = 0; n < tx.size(); ++n) {
        cf32 acc{};
        for (int k = 0; k < 3; ++k) {
            const long idx = static_cast<long>(n) + 1 - k;  // centred channel
            if (idx >= 0 && idx < static_cast<long>(tx.size())) acc += h[k] * tx[static_cast<std::size_t>(idx)];
        }
        rx[n] = acc * std::polar(1.0f, static_cast<float>(1.1 + w * static_cast<double>(n)));
    }

    for (int delay : {0, 2}) {
        TwoPassEqualizer eq(uw, 216, delay);
        EqualizerResult r;
        CHECK(eq.process(rx, r));
        int errs = 0;
        for (std::size_t i = 0; i < 216; ++i) errs += qam256::slice(r.data[i]) != bytes[i];
        std::printf("  delay %d: UW MER %.1f dB, CFO est %.1f Hz (true 200), cond %.1f, symbol errors %d\n", delay,
                    r.uw_mer_db, r.cfo_rad_per_sym * SYMBOL_RATE / (2 * PI), r.cond_estimate, errs);
        if (delay == 2) {
            CHECK(errs == 0);
            CHECK(r.uw_mer_db > 30.0);
            CHECK(std::abs(r.cfo_rad_per_sym * SYMBOL_RATE / (2 * PI) - 200.0) < 5.0);
        }
    }
}

struct LoopbackResult {
    std::size_t warmup = 0;
    std::size_t frames = 0, ok = 0, uncorrectable = 0, rs_fixed = 0, pre_bits = 0, post_bits = 0;
    int max_fixed = 0;
    double mean_mer = 0.0;
    bool spacing_ok = true;
    double snr_db = NAN;
    std::size_t symbols = 0, samples = 0;
};

// Single-threaded run of the full chain (same stage classes the threaded
// receiver uses).
LoopbackResult loopback(const ChannelParams& ch, double seconds, RxConfig cfg = {}) {
    const auto uw = default_unique_word();
    TxSimulator tx(uw, GOLDEN_PAYLOAD, ch, cfg.rs_fcr);
    FrontEnd fe(cfg);
    SyncCorrelator corr(uw, DATA_LENGTH_SYM, cfg.corr_threshold);
    TwoPassEqualizer eq(uw, DATA_LENGTH_SYM, cfg.eq_delay, cfg.eq_lambda, cfg.cfo_correction);
    FrameDecoder dec(GOLDEN_PAYLOAD, cfg.rs_fcr);
    SnrEstimator snr(SAMPLE_RATE, cfg.snr_avg_len, cfg.snr_report_block, cfg.snr_decimation);

    LoopbackResult res;
    std::vector<cf32> in(RX_CHUNK_SAMPLES), dc(RX_CHUNK_SAMPLES), syms;
    std::size_t last_idx = 0;
    const std::size_t total = static_cast<std::size_t>(seconds * SAMPLE_RATE);
    const std::size_t seg_len = corr.segment_len();
    for (std::size_t done = 0; done < total; done += RX_CHUNK_SAMPLES) {
        tx.generate(in.data(), in.size());
        syms.clear();
        fe.process(in.data(), in.size(), dc.data(), syms);
        if (snr.process(dc.data(), dc.size())) res.snr_db = snr.snr_db();
        res.samples += in.size();
        res.symbols += syms.size();
        corr.push(syms.data(), syms.size(), {}, [&](Segment&& seg) {
            if (res.frames + res.warmup > 0) {
                const std::size_t gap = seg.stream_index - last_idx;
                // Gate 4: consecutive segments exactly one segment apart
                // (+-1 symbol when the timing loop slips a symbol).
                if (gap + 1 < seg_len || gap > seg_len + 1) res.spacing_ok = false;
            }
            last_idx = seg.stream_index;
            EqualizerResult er;
            if (!eq.process(seg.symbols, er)) return;
            const DecodeResult d = dec.decode(er.data);
            if (res.warmup < cfg.warmup_frames) {
                ++res.warmup;
                return;
            }
            ++res.frames;
            res.mean_mer += er.uw_mer_db;
            res.pre_bits += d.pre_fec_bit_errors;
            res.post_bits += d.post_fec_bit_errors;
            if (d.rs_corrected < 0) {
                ++res.uncorrectable;
            } else {
                res.rs_fixed += static_cast<std::size_t>(d.rs_corrected);
                res.max_fixed = std::max(res.max_fixed, d.rs_corrected);
            }
            if (d.post_fec_bit_errors == 0) ++res.ok;
        });
    }
    if (res.frames) res.mean_mer /= static_cast<double>(res.frames);
    return res;
}

void print(const char* tag, const LoopbackResult& r, double seconds) {
    const double expected = seconds * SYMBOL_RATE / (2.0 * 143 + 216);
    std::printf("  %-28s frames %zu/~%.0f ok %zu unc %zu | RS fixed %zu (max %d) | BER pre %.2e post %.2e | "
                "MER %.1f dB | SNR %.1f dB | sym/sample %.4f\n",
                tag, r.frames, expected, r.ok, r.uncorrectable, r.rs_fixed, r.max_fixed,
                r.frames ? double(r.pre_bits) / (r.frames * 1728.0) : NAN,
                r.frames ? double(r.post_bits) / (r.frames * 1600.0) : NAN, r.mean_mer, r.snr_db,
                double(r.symbols) / double(r.samples));
}

void test_loopback_clean() {
    ChannelParams ch;
    ch.esn0_db = 300;  // noiseless
    ch.cfo_hz = 0;
    ch.clock_ppm = 0;
    const double secs = 1.0;
    const auto r = loopback(ch, secs);
    print("clean", r, secs);
    CHECK(r.frames > 650);
    CHECK(r.post_bits == 0);
    CHECK(r.pre_bits == 0);
    CHECK(r.spacing_ok);
    CHECK(std::abs(double(r.symbols) / double(r.samples) - 0.25) < 1e-3);  // gate 3: 1 sample / symbol
}

void test_loopback_impaired() {
    ChannelParams ch;
    ch.esn0_db = 38;
    ch.cfo_hz = 400;
    ch.clock_ppm = 10;
    ch.echo = {0.15f, 0.1f};
    const double secs = 2.0;
    const auto r = loopback(ch, secs);
    print("38 dB, 400 Hz, 10 ppm, echo", r, secs);
    CHECK(r.frames > 1300);
    CHECK(r.post_bits == 0);          // gate 6: BER = 0
    CHECK(r.max_fixed < 8);           //          correctable errors < 8 per block
    CHECK(r.spacing_ok);              // gate 4
    CHECK(r.mean_mer > 28.0);         // gate 5
}

void test_loopback_rs_working() {
    // Low enough SNR that the slicer makes errors and RS has to fix them.
    ChannelParams ch;
    ch.esn0_db = 29;
    ch.cfo_hz = 150;
    ch.clock_ppm = 5;
    ch.echo = {0.1f, -0.05f};
    const double secs = 2.0;
    const auto r = loopback(ch, secs);
    print("29 dB (RS active)", r, secs);
    CHECK(r.frames > 1300);
    CHECK(r.pre_bits > 0);
    CHECK(r.rs_fixed > 0);
    CHECK(r.post_bits * 100 < r.pre_bits);
}

void test_snr_estimator() {
    // Radiometer against a known Es/N0: signal power in its 480 kHz band over
    // the noise in an equal band, i.e. roughly Es/N0 * Rs/B.
    for (double esn0 : {15.0, 25.0}) {
        ChannelParams ch;
        ch.esn0_db = esn0;
        ch.dc_offset = {0, 0};
        TxSimulator tx(default_unique_word(), GOLDEN_PAYLOAD, ch);
        DcBlocker dc(32);
        SnrEstimator snr(SAMPLE_RATE, 100000, 10000, 4);
        std::vector<cf32> x(RX_CHUNK_SAMPLES);
        for (int i = 0; i < 200; ++i) {
            tx.generate(x.data(), x.size());
            dc.process(x.data(), x.data(), x.size());
            snr.process(x.data(), x.size());
        }
        // Noise density is flat over Fs; Es/N0 -> in-band SNR over 480 kHz.
        const double expect = esn0 + 10 * std::log10(SYMBOL_RATE / 480e3);
        std::printf("  Es/N0 %.0f dB: radiometer %.2f dB (expected ~%.2f dB)\n", esn0, snr.snr_db(), expect);
        CHECK(std::abs(snr.snr_db() - expect) < 1.0);
    }
}

void test_spectrum() {
    // A +300 kHz tone must peak in the fft-shifted bin for +300 kHz.
    SpectrumEstimator spec(1024);
    std::vector<cf32> x(8192);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = std::polar(0.5f, static_cast<float>(2 * PI * 300e3 / SAMPLE_RATE * static_cast<double>(i)));
    for (int k = 0; k < 4; ++k) spec.process(x.data(), x.size());
    std::vector<float> psd;
    spec.psd_db(psd);
    const auto peak = static_cast<std::size_t>(std::max_element(psd.begin(), psd.end()) - psd.begin());
    const std::size_t expect = 512 + static_cast<std::size_t>(std::lround(300e3 / SAMPLE_RATE * 1024));
    std::printf("  +300 kHz tone peak at bin %zu (expected %zu), %.1f dB\n", peak, expect, psd[peak]);
    CHECK(peak + 1 >= expect && peak <= expect + 1);
    CHECK(std::abs(psd[peak] - 20.0f * std::log10(0.5f)) < 2.0f);
}

void test_dashboard_api() {
    RxConfig cfg;
    const auto uw = default_unique_word();
    Receiver rx(cfg, uw, GOLDEN_PAYLOAD);
    SimSource src(std::make_unique<TxSimulator>(uw, GOLDEN_PAYLOAD, ChannelParams{}), 0);
    UiState ui;
    Dashboard dash(rx, src, ui, "127.0.0.1", "numpy");
    auto req = [](std::string method, std::string path, std::string host, std::string body = "", bool hdr = false) {
        HttpRequest r;
        r.method = std::move(method);
        r.path = std::move(path);
        r.headers["host"] = std::move(host);
        if (hdr) r.headers["x-aiw-control"] = "1";
        r.body = std::move(body);
        return r;
    };
    CHECK(dash.handle(req("GET", "/", "localhost:8080")).status == 200);
    CHECK(dash.handle(req("GET", "/", "localhost:8080")).body.find("AIW-Rx") != std::string::npos);
    const auto st = dash.handle(req("GET", "/api/status", "127.0.0.1:8080"));
    CHECK(st.status == 200 && st.body.find("\"uw_length\":143") != std::string::npos);
    CHECK(st.body.find("nan") == std::string::npos);  // NaN serialised as null
    CHECK(dash.handle(req("GET", "/api/status", "evil.example")).status == 403);          // DNS rebinding
    CHECK(dash.handle(req("POST", "/api/control", "localhost", "{\"esn0\":20}")).status == 403);  // no header
    CHECK(dash.handle(req("POST", "/api/control", "localhost", "{\"freq\":915e6}", true)).status == 400);
    CHECK(dash.handle(req("POST", "/api/control", "localhost", "{\"esn0\":-1}", true)).status == 400);
    CHECK(dash.handle(req("POST", "/api/control", "localhost", "{\"esn0\":21.5}", true)).status == 200);
    CHECK(std::abs(src.sim_esn0() - 21.5) < 1e-9);
    CHECK(dash.handle(req("GET", "/nope", "localhost")).status == 404);
}

}  // namespace

int main() {
    run("unique word = Zadoff-Chu(143,25)", test_unique_word);
    run("golden payload", test_golden_payload);
    run("256-QAM map/slice", test_qam256);
    run("Reed-Solomon RS(255,239) shortened", test_reed_solomon);
    run("Reed-Solomon parity matches reedsolo", test_reed_solomon_interop);
    run("SPSC ring buffer", test_ring_buffer);
    run("DC blocker + AGC", test_dc_blocker_and_agc);
    run("frequency translation FIR", test_xlating_filter);
    run("two-pass LLS equaliser", test_equalizer_synthetic);
    run("SNR radiometer", test_snr_estimator);
    run("spectrum estimator", test_spectrum);
    run("dashboard API", test_dashboard_api);
    run("loopback: clean channel", test_loopback_clean);
    run("loopback: impaired channel, BER 0", test_loopback_impaired);
    run("loopback: RS correcting", test_loopback_rs_working);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
