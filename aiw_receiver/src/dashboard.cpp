#include "dashboard.hpp"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace aiw {

namespace {

// NaN/Inf-safe number formatting.  Checks the IEEE exponent bits directly
// because -ffast-math lets the compiler assume std::isfinite() is true.
bool finite(double v) { return (std::bit_cast<uint64_t>(v) & 0x7FF0000000000000ull) != 0x7FF0000000000000ull; }

class Json {
public:
    Json& key(const char* k) {
        sep();
        os_ << '"' << k << "\":";
        fresh_ = true;
        return *this;
    }
    Json& num(double v, int prec = 6) {
        sep();
        if (finite(v))
            os_ << std::setprecision(prec) << v;
        else
            os_ << "null";
        return *this;
    }
    Json& integer(uint64_t v) {
        sep();
        os_ << v;
        return *this;
    }
    Json& boolean(bool b) {
        sep();
        os_ << (b ? "true" : "false");
        return *this;
    }
    Json& str(std::string_view s) {
        sep();
        os_ << '"';
        for (char ch : s) {
            if (ch == '"' || ch == '\\')
                os_ << '\\' << ch;
            else if (static_cast<unsigned char>(ch) < 0x20)
                os_ << ' ';
            else
                os_ << ch;
        }
        os_ << '"';
        return *this;
    }
    Json& open(char c) {
        sep();
        os_ << c;
        fresh_ = true;
        return *this;
    }
    Json& close(char c) {
        os_ << c;
        fresh_ = false;
        return *this;
    }
    std::string done() const { return os_.str(); }

private:
    void sep() {
        if (!fresh_) os_ << ',';
        fresh_ = false;
    }
    std::ostringstream os_;
    bool fresh_ = true;
};

// Extracts a numeric value for "key" from a flat JSON object.
bool json_number(const std::string& body, const char* key, double& out) {
    const std::string k = std::string("\"") + key + "\"";
    const auto p = body.find(k);
    if (p == std::string::npos) return false;
    const auto colon = body.find(':', p + k.size());
    if (colon == std::string::npos) return false;
    const char* start = body.c_str() + colon + 1;
    char* end = nullptr;
    out = std::strtod(start, &end);
    return end != start && finite(out);
}

HttpResponse json_response(std::string body) { return {200, "application/json", std::move(body)}; }
HttpResponse error(int status, const std::string& msg) {
    Json j;
    j.open('{').key("ok").boolean(false).key("message").str(msg).close('}');
    return {status, "application/json", j.done()};
}

}  // namespace

Dashboard::Dashboard(const Receiver& rx, SampleSource& src, UiState& ui, std::string bind_addr,
                     std::string golden_name)
    : rx_(rx), src_(src), ui_(ui), bind_addr_(std::move(bind_addr)), golden_name_(std::move(golden_name)) {}

HttpResponse Dashboard::handle(const HttpRequest& req) {
    // DNS-rebinding guard for the default loopback binding.
    if (bind_addr_ == "127.0.0.1") {
        auto it = req.headers.find("host");
        const std::string host = it == req.headers.end() ? "" : it->second;
        if (host.rfind("localhost", 0) != 0 && host.rfind("127.0.0.1", 0) != 0) return error(403, "bad host");
    }
    if (req.method == "GET") {
        if (req.path == "/" || req.path == "/index.html")
            return {200, "text/html; charset=utf-8", std::string(dashboard_page())};
        if (req.path == "/api/status") return status();
        if (req.path == "/api/constellation") return constellation();
        if (req.path == "/api/spectrum") return spectrum();
        if (req.path == "/api/history") return history(req);
        return error(404, "not found");
    }
    if (req.method == "POST" && req.path == "/api/control") return control(req);
    return error(405, "method not allowed");
}

HttpResponse Dashboard::status() {
    const Metrics& m = rx_.metrics();
    const RxConfig& c = rx_.config();
    const double up = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
    bool finished;
    HistoryPoint last;
    bool have_last;
    {
        std::lock_guard lk(ui_.mu);
        finished = ui_.run_finished;
        have_last = !ui_.history.empty();
        if (have_last) last = ui_.history.back();
    }
    const uint64_t frames = m.frames_decoded.load();
    Json j;
    j.open('{');
    j.key("uptime_s").num(up).key("run_finished").boolean(finished);
    j.key("source").open('{')
        .key("kind").str(src_.kind())
        .key("describe").str(src_.describe())
        .key("center_freq").num(src_.center_freq(), 12)
        .key("gain").num(src_.gain())
        .key("sim_esn0").num(src_.sim_esn0())
        .key("sim_cfo").num(src_.sim_cfo())
        .close('}');
    j.key("totals").open('{')
        .key("samples").integer(m.samples_in.load())
        .key("symbols").integer(m.symbols_out.load())
        .key("warmup_frames").integer(m.warmup_frames.load())
        .key("frames").integer(frames)
        .key("frames_ok").integer(m.frames_payload_ok.load())
        .key("uncorrectable").integer(m.frames_uncorrectable.load())
        .key("rs_corrected").integer(m.rs_corrected_symbols.load())
        .key("pre_fec_ber").num(frames ? double(m.pre_fec_bit_errors.load()) / (frames * 1728.0) : NAN)
        .key("post_fec_ber").num(frames ? double(m.post_fec_bit_errors.load()) / (frames * 1600.0) : NAN)
        .key("corr_peaks").integer(m.corr_peaks.load())
        .key("unconfirmed_peaks").integer(m.unconfirmed_peaks.load())
        .key("eq_failures").integer(m.eq_failures.load())
        .key("dropped").integer(m.chunks_dropped.load() + m.symbol_chunks_dropped.load() + m.frames_dropped.load())
        .key("overflows").integer(m.usrp_overflows.load())
        .close('}');
    j.key("live").open('{')
        .key("snr_db").num(m.snr_db.load())
        .key("mer_db").num(m.uw_mer_db.load())
        .key("cfo_hz").num(m.cfo_hz.load())
        .key("corr_metric").num(m.corr_metric.load())
        .key("eq_cond").num(m.eq_cond.load())
        .key("agc_gain").num(m.agc_gain.load())
        .key("pfb_rate").num(m.pfb_rate.load())
        .close('}');
    j.key("interval").open('{');
    if (have_last) {
        j.key("frames_per_s").num(last.frames_per_s)
            .key("pre_ber").num(last.pre_ber)
            .key("post_ber").num(last.post_ber)
            .key("lat_mean_ms").num(last.lat_mean_ms)
            .key("lat_max_ms").num(last.lat_max_ms)
            .key("uncorrectable").num(last.uncorrectable)
            .key("rs_max").num(last.rs_max);
    }
    j.close('}');
    j.key("config").open('{')
        .key("sample_rate").num(c.sample_rate, 12)
        .key("symbol_rate").num(c.sample_rate / SPS, 12)
        .key("uw_length").integer(rx_.uw_length())
        .key("segment_length").integer(2 * rx_.uw_length() + DATA_LENGTH_SYM)
        .key("xlat_offset_hz").num(c.xlat_offset_hz, 12)
        .key("agc_attack").num(c.agc_attack)
        .key("agc_decay").num(c.agc_decay)
        .key("pfb_loop_bw").num(c.pfb_loop_bw)
        .key("pfb_damping").num(c.pfb_damping)
        .key("corr_threshold").num(c.corr_threshold)
        .key("eq_delay").integer(static_cast<uint64_t>(c.eq_delay))
        .key("eq_lambda").num(c.eq_lambda)
        .key("cfo_correction").boolean(c.cfo_correction)
        .key("fec_batch").integer(c.fec_batch)
        .key("rs_fcr").integer(static_cast<uint64_t>(c.rs_fcr))
        .key("warmup_frames").integer(c.warmup_frames)
        .key("golden").str(golden_name_)
        .close('}');
    j.close('}');
    return json_response(j.done());
}

HttpResponse Dashboard::constellation() {
    std::vector<cf32> pts;
    std::array<cf32, EQ_TAPS> taps;
    {
        std::lock_guard lk(ui_.mu);
        const std::size_t n = ui_.const_count, N = UiState::CONSTELLATION_POINTS;
        pts.reserve(n);
        for (std::size_t i = 0; i < n; ++i) pts.push_back(ui_.constellation[(ui_.const_pos + N - n + i) % N]);
        taps = ui_.taps;
    }
    Json j;
    j.open('{').key("points").open('[');
    for (const cf32& p : pts) j.num(p.real(), 4).num(p.imag(), 4);
    j.close(']').key("taps").open('[');
    for (const cf32& t : taps) j.num(t.real(), 5).num(t.imag(), 5);
    j.close(']').close('}');
    return json_response(j.done());
}

HttpResponse Dashboard::spectrum() {
    std::vector<float> psd;
    {
        std::lock_guard lk(ui_.mu);
        psd = ui_.psd_db;
    }
    Json j;
    j.open('{').key("sample_rate").num(rx_.config().sample_rate, 12).key("psd_db").open('[');
    for (float v : psd) j.num(v, 4);
    j.close(']').close('}');
    return json_response(j.done());
}

HttpResponse Dashboard::history(const HttpRequest& req) {
    double since = -1.0;
    if (req.query.rfind("since=", 0) == 0) since = std::strtod(req.query.c_str() + 6, nullptr);
    Json j;
    j.open('{').key("points").open('[');
    {
        std::lock_guard lk(ui_.mu);
        for (const HistoryPoint& h : ui_.history) {
            if (h.t <= since) continue;
            j.open('{')
                .key("t").num(h.t)
                .key("fps").num(h.frames_per_s, 5)
                .key("pre").num(h.pre_ber, 4)
                .key("post").num(h.post_ber, 4)
                .key("snr").num(h.snr_db, 4)
                .key("mer").num(h.mer_db, 4)
                .key("cfo").num(h.cfo_hz, 5)
                .key("lat").num(h.lat_mean_ms, 4)
                .key("latmax").num(h.lat_max_ms, 4)
                .key("unc").num(h.uncorrectable)
                .key("rsmax").num(h.rs_max)
                .close('}');
        }
    }
    j.close(']').close('}');
    return json_response(j.done());
}

HttpResponse Dashboard::control(const HttpRequest& req) {
    auto h = req.headers.find("x-aiw-control");
    if (h == req.headers.end() || h->second != "1") return error(403, "missing X-AIW-Control header");

    std::string applied;
    double v;
    if (json_number(req.body, "freq", v)) {
        if (v < 70e6 || v > 6e9) return error(400, "frequency must be 70 MHz - 6 GHz");
        if (!src_.set_center_freq(v)) return error(400, "this source cannot be tuned");
        applied += "frequency ";
    }
    if (json_number(req.body, "gain", v)) {
        if (v < 0 || v > 89) return error(400, "gain must be 0 - 89 dB");
        if (!src_.set_gain(v)) return error(400, "this source has no RF gain");
        applied += "gain ";
    }
    if (json_number(req.body, "esn0", v)) {
        if (v < 0 || v > 300) return error(400, "Es/N0 must be 0 - 300 dB");
        if (!src_.set_sim_esn0(v)) return error(400, "Es/N0 applies to the simulator only");
        applied += "Es/N0 ";
    }
    if (json_number(req.body, "cfo", v)) {
        if (v < -20000 || v > 20000) return error(400, "CFO must be within +-20 kHz");
        if (!src_.set_sim_cfo(v)) return error(400, "CFO applies to the simulator only");
        applied += "CFO ";
    }
    if (applied.empty()) return error(400, "nothing to change");
    applied.pop_back();
    Json j;
    j.open('{').key("ok").boolean(true).key("message").str("applied " + applied).close('}');
    return json_response(j.done());
}

}  // namespace aiw
