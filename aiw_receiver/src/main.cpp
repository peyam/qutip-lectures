// aiw_rx - standalone AIW 256-QAM receiver.
//
//   aiw_rx [--source usrp|file|sim] [options]
//
// See README.md for the full option list and the verification procedure.

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "config.hpp"
#include "dashboard.hpp"
#include "golden_payload.hpp"
#include "receiver.hpp"
#include "sample_source.hpp"
#include "unique_word.hpp"
#include "usrp_source.hpp"

namespace {

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }

const char* USAGE = R"(usage: aiw_rx [options]

Source selection
  --source S            usrp | file | sim (default: usrp if built with UHD, else sim)
  --file PATH           raw interleaved complex float32 input (with --source file)
  --loop                loop the input file
  --realtime            pace file/sim input at the sample rate (for latency measurements)

Radio (spec 7)
  --args STR            UHD device args            (default serial=3273A14)
  --subdev STR          RX subdevice spec          (default: device default)
  --antenna STR         RX antenna                 (default RX2)
  --freq HZ             centre frequency           (default 917e6)
  --gain DB             RF gain                    (default 45)
  --rate SPS            sample rate                (default 1.4e6)

Receiver
  --xlat-offset HZ      frequency translation offset (default 300000)
  --agc-attack X        (default 1e-3; spec 0.1)   --agc-decay X (default 1e-3; spec 0.001)
  --loop-bw X           PFB loop bandwidth (default 2*pi/100)
  --damping X           PFB damping factor (default 64 = GNU Radio 2*nfilts; spec 0.707)
  --corr-threshold X    normalised UW correlation threshold 0..1 (default 0.35)
  --eq-delay N          equaliser decision delay 0..4 (default 2; 0 = causal)
  --eq-lambda X         Tikhonov regularisation (default 1e-4)
  --no-cfo              disable UW-aided frequency correction
  --uw-zc N:ROOT        use a Zadoff-Chu UW of length N instead of the spec table
  --fec-batch N         codewords per decoding run (default 8)
  --rs-fcr N            RS first consecutive root (default 0)
  --warmup-frames N     frames excluded from BER while loops converge (default 16)
  --golden numpy|spec   ground-truth payload table (default numpy)
  --golden-file PATH    200-byte raw ground-truth payload

Simulation (--source sim)
  --sim-esn0 DB         Es/N0 (default 40; >= 200 disables noise)
  --sim-cfo HZ          residual carrier offset (default 150)
  --sim-ppm X           sample clock offset in ppm (default 5)
  --sim-echo RE,IM      one-symbol-delayed echo gain (default 0.15,0.1)
  --sim-seconds S       amount of signal to generate (default 5; 0 = endless)

Dashboard
  --ui                  serve the live web dashboard (http://localhost:8080)
  --ui-port N           dashboard port (default 8080)
  --ui-bind ADDR        listen address (default 127.0.0.1; 0.0.0.0 exposes it to
                        the network - there is no authentication)
                        With --ui, --source sim runs endlessly in real time unless
                        --sim-seconds is given.

Run control / output
  --duration S          stop after S seconds of wall time
  --interval S          metrics report interval (default 1)
  --csv PATH            write per-interval metrics to CSV
  --expect-ber0         exit non-zero unless frames were decoded with post-FEC BER = 0
  --quiet
)";

aiw::cf32 parse_complex(const std::string& s) {
    const auto comma = s.find(',');
    if (comma == std::string::npos) return {std::stof(s), 0.0f};
    return {std::stof(s.substr(0, comma)), std::stof(s.substr(comma + 1))};
}

}  // namespace

int main(int argc, char** argv) {
    using namespace aiw;
    std::map<std::string, std::string> kv;
    const std::map<std::string, bool> flags = {{"--loop", true},  {"--no-cfo", true}, {"--expect-ber0", true},
                                               {"--quiet", true}, {"--realtime", true}, {"--ui", true}, {"--help", true},   {"-h", true}};
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (flags.count(a)) {
            kv[a] = "1";
        } else if (a.rfind("--", 0) == 0 && i + 1 < argc) {
            kv[a] = argv[++i];
        } else {
            std::cerr << "unknown or incomplete argument: " << a << "\n" << USAGE;
            return 2;
        }
    }
    if (kv.count("--help") || kv.count("-h")) {
        std::cout << USAGE;
        return 0;
    }
    auto get = [&](const char* k, const std::string& def) { return kv.count(k) ? kv[k] : def; };
    auto getd = [&](const char* k, double def) { return kv.count(k) ? std::stod(kv[k]) : def; };

    try {
        RxConfig cfg;
        cfg.device_args = get("--args", cfg.device_args);
        cfg.subdev = get("--subdev", cfg.subdev);
        cfg.antenna = get("--antenna", cfg.antenna);
        cfg.center_freq = getd("--freq", cfg.center_freq);
        cfg.gain = getd("--gain", cfg.gain);
        cfg.sample_rate = getd("--rate", cfg.sample_rate);
        cfg.xlat_offset_hz = getd("--xlat-offset", cfg.xlat_offset_hz);
        cfg.agc_attack = static_cast<float>(getd("--agc-attack", cfg.agc_attack));
        cfg.agc_decay = static_cast<float>(getd("--agc-decay", cfg.agc_decay));
        cfg.pfb_loop_bw = getd("--loop-bw", cfg.pfb_loop_bw);
        cfg.pfb_damping = getd("--damping", cfg.pfb_damping);
        cfg.corr_threshold = static_cast<float>(getd("--corr-threshold", cfg.corr_threshold));
        cfg.eq_delay = static_cast<int>(getd("--eq-delay", cfg.eq_delay));
        cfg.eq_lambda = getd("--eq-lambda", cfg.eq_lambda);
        cfg.cfo_correction = !kv.count("--no-cfo");
        cfg.fec_batch = static_cast<std::size_t>(getd("--fec-batch", static_cast<double>(cfg.fec_batch)));
        cfg.rs_fcr = static_cast<int>(getd("--rs-fcr", cfg.rs_fcr));
        cfg.warmup_frames = static_cast<std::size_t>(getd("--warmup-frames", static_cast<double>(cfg.warmup_frames)));
        if (cfg.sample_rate != SAMPLE_RATE)
            std::cerr << "[aiw_rx] WARNING: filters and timing are designed for 1.4 MSps / 4 sps\n";
        if (cfg.eq_delay < 0 || cfg.eq_delay > static_cast<int>(SEGMENT_MARGIN))
            throw std::runtime_error("--eq-delay must be 0..4");

        std::vector<cf32> uw = default_unique_word();
        if (kv.count("--uw-zc")) {
            const std::string s = kv["--uw-zc"];
            const auto c = s.find(':');
            if (c == std::string::npos) throw std::runtime_error("--uw-zc expects N:ROOT");
            uw = make_zadoff_chu(std::stoi(s.substr(0, c)), std::stoi(s.substr(c + 1)));
        }

        std::array<uint8_t, PAYLOAD_SIZE> golden = GOLDEN_PAYLOAD;
        if (get("--golden", "numpy") == "spec") golden = GOLDEN_PAYLOAD_SPEC_TABLE;
        if (kv.count("--golden-file")) {
            std::ifstream f(kv["--golden-file"], std::ios::binary);
            if (!f.read(reinterpret_cast<char*>(golden.data()), golden.size()))
                throw std::runtime_error("--golden-file must contain 200 bytes");
        }

#ifdef AIW_HAVE_UHD
        const std::string default_source = "usrp";
#else
        const std::string default_source = "sim";
#endif
        const std::string source = get("--source", default_source);
        std::unique_ptr<SampleSource> src;
        if (source == "usrp") {
#ifdef AIW_HAVE_UHD
            UsrpConfig uc{cfg.device_args, cfg.subdev, cfg.antenna, cfg.sample_rate, cfg.center_freq, cfg.gain};
            src = std::make_unique<UsrpSource>(uc);
#else
            throw std::runtime_error("built without UHD; use --source file or --source sim");
#endif
        } else if (source == "file") {
            if (!kv.count("--file")) throw std::runtime_error("--source file requires --file PATH");
            src = std::make_unique<FileSource>(kv["--file"], kv.count("--loop") > 0);
        } else if (source == "sim") {
            ChannelParams ch;
            ch.esn0_db = getd("--sim-esn0", 40.0);
            ch.cfo_hz = getd("--sim-cfo", 150.0);
            ch.clock_ppm = getd("--sim-ppm", 5.0);
            ch.echo = parse_complex(get("--sim-echo", "0.15,0.1"));
            ch.if_offset_hz = cfg.xlat_offset_hz;
            const double secs = getd("--sim-seconds", kv.count("--ui") ? 0.0 : 5.0);
            auto tx = std::make_unique<TxSimulator>(uw, golden, ch, cfg.rs_fcr);
            src = std::make_unique<SimSource>(std::move(tx), static_cast<std::size_t>(secs * SAMPLE_RATE));
        } else {
            throw std::runtime_error("unknown --source " + source);
        }

        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);

        RunOptions opt;
        opt.duration_s = getd("--duration", 0.0);
        opt.report_interval_s = getd("--interval", 1.0);
        opt.csv_path = get("--csv", "");
        opt.quiet = kv.count("--quiet") > 0;
        opt.realtime = kv.count("--realtime") > 0 || (kv.count("--ui") && source == "sim");
        opt.external_stop = &g_stop;

        Receiver rx(cfg, uw, golden);
        UiState ui;
        std::unique_ptr<Dashboard> dash;
        std::unique_ptr<HttpServer> http;
        if (kv.count("--ui")) {
            const std::string bind = get("--ui-bind", "127.0.0.1");
            const int port = static_cast<int>(getd("--ui-port", 8080));
            std::string golden_name = kv.count("--golden-file") ? "file" : get("--golden", "numpy");
            dash = std::make_unique<Dashboard>(rx, *src, ui, bind, golden_name);
            http = std::make_unique<HttpServer>(bind, port, [&](const HttpRequest& r) { return dash->handle(r); });
            http->start();
            opt.ui = &ui;
            std::cout << "[aiw_rx] dashboard: http://" << (bind == "0.0.0.0" ? "localhost" : bind) << ":" << port
                      << "/" << std::endl;
        }
        const RunSummary s = rx.run(*src, opt);

        std::cout << "\n=== AIW-Rx summary ===\n"
                  << "samples            " << s.samples << "\n"
                  << "warm-up frames     " << s.warmup_frames << " (excluded from BER)\n"
                  << "frames decoded     " << s.frames << "\n"
                  << "payload exact      " << s.payload_ok << "\n"
                  << "uncorrectable      " << s.uncorrectable << "\n"
                  << "max RS corrections " << s.max_rs_corrected << " per block\n"
                  << "pre-FEC BER        " << s.pre_fec_ber() << "\n"
                  << "post-FEC BER       " << s.post_fec_ber() << "\n"
                  << "radiometric SNR    " << s.last_snr_db << " dB\n"
                  << "UW MER             " << s.last_mer_db << " dB\n"
                  << "latency mean/max   " << s.mean_latency_ms << " / " << s.max_latency_ms << " ms\n"
                  << "dropped chunks     " << s.dropped_chunks << "\n"
                  << "USRP overflows     " << s.overflows << "\n";

        if (http && !g_stop) {
            std::cout << "\n[aiw_rx] input finished; dashboard still available. Press Ctrl+C to exit.\n" << std::flush;
            while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (http) http->stop();

        if (kv.count("--expect-ber0") && (s.frames == 0 || s.post_fec_bit_errors != 0)) {
            std::cerr << "FAIL: expected error-free decoding\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
