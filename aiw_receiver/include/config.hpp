// Global system parameters for the AIW-Rx receiver (spec sections 2.1 / 2.2).
#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>

namespace aiw {

using cf32 = std::complex<float>;

// ---- 2.1 Hardware and physical layer ---------------------------------------
inline constexpr double SAMPLE_RATE      = 1'400'000.0;
inline constexpr int    SPS              = 4;
inline constexpr double SYMBOL_RATE      = SAMPLE_RATE / SPS;          // 350 kBd
inline constexpr double RX_CENTER_FREQ   = 917e6;
inline constexpr double RF_GAIN_RX       = 45.0;
inline constexpr double RRC_ALPHA        = 0.35;
inline constexpr int    N_FILTS          = 32;
inline constexpr int    NSYM_FILT        = 16;
inline constexpr double OCCUPIED_BW      = SYMBOL_RATE * (255.0 / 239.0) * (1.0 + RRC_ALPHA);
inline constexpr double FREQ_XLAT_OFFSET = 300'000.0;

// ---- 2.2 Framing / FEC -----------------------------------------------------
inline constexpr std::size_t DATA_LENGTH_SYM     = 216;
inline constexpr std::size_t RS_N                = 255;
inline constexpr std::size_t RS_K                = 239;
inline constexpr std::size_t RS_PARITY           = RS_N - RS_K;       // 16
inline constexpr std::size_t PAYLOAD_SIZE        = 200;
inline constexpr std::size_t SHORTENING_SIZE     = RS_K - PAYLOAD_SIZE; // 39
inline constexpr std::size_t CODEWORD_SIZE_SHORT = PAYLOAD_SIZE + RS_PARITY; // 216
inline constexpr std::size_t FEC_BATCH_SIZE      = 8;

static_assert(CODEWORD_SIZE_SHORT == DATA_LENGTH_SYM, "one byte per 256-QAM symbol");

// ---- Pipeline sizing -------------------------------------------------------
inline constexpr std::size_t RX_CHUNK_SAMPLES = 8192;
inline constexpr std::size_t SYMBOL_CHUNK_MAX = 4096;
inline constexpr int         EQ_TAPS          = 5;
// Extra symbols carried on each side of an extracted segment so the
// equalizer has the history/future it needs at the segment edges.
inline constexpr std::size_t SEGMENT_MARGIN   = EQ_TAPS - 1;

// Runtime-tunable receiver configuration.  Defaults follow the specification;
// deviations are documented in README.md.
struct RxConfig {
    // Radio
    std::string device_args = "serial=3273A14";
    std::string subdev;                // empty = device default (spec lists "0:A")
    std::string antenna     = "RX2";
    double sample_rate      = SAMPLE_RATE;
    double center_freq      = RX_CENTER_FREQ;
    double gain             = RF_GAIN_RX;

    // Front end
    double dc_blocker_len   = 32.0;
    double xlat_offset_hz   = FREQ_XLAT_OFFSET;
    // Spec 4.3 gives attack 0.1 / decay 0.001.  With the multiplicative
    // per-sample update those rates modulate the gain *within* a frame and
    // cap 256-QAM MER at ~23 dB (every frame uncorrectable in simulation), so
    // the defaults are slower.  `--agc-attack 0.1 --agc-decay 0.001`
    // restores the spec values.
    float agc_attack        = 1e-3f;
    float agc_decay         = 1e-3f;
    float agc_reference     = 1.0f;
    float agc_max_gain      = 65536.0f;

    // Timing recovery
    double pfb_loop_bw      = 2.0 * 3.14159265358979323846 / 100.0;
    // GNU Radio's pfb_clock_sync sets damping = 2 * nfilts internally (the
    // GRC block does not expose it), so that is the default here.  The
    // spec's 0.707 is available via `--damping 0.707`; it converges roughly
    // 5x slower and interacts badly with a fast AGC.
    double pfb_damping      = 2.0 * N_FILTS;
    double pfb_max_dev      = 1.5;

    // Frame sync / equalizer
    float corr_threshold    = 0.35f;  // normalised |C|^2 / (L * E) in [0, 1]
    int eq_delay            = 2;      // decision delay (0 = causal as written in 5.3)
    double eq_lambda        = 1e-4;
    bool cfo_correction     = true;

    // FEC
    std::size_t fec_batch   = FEC_BATCH_SIZE;
    int rs_fcr              = 0;      // DVB-T / gr-dtv convention
    // Frames decoded while AGC / timing loop are still converging are
    // reported separately and excluded from BER statistics.
    std::size_t warmup_frames = 16;

    // SNR radiometer
    double snr_avg_len      = 100'000.0;
    std::size_t snr_report_block = 10'000;
    int snr_decimation      = 4;      // evaluate band filters every Nth sample
};

}  // namespace aiw
