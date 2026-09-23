// Web dashboard (`aiw_rx --ui`): serves the embedded single-page UI and a
// small JSON API backed by the receiver's Metrics and UiState.
//
//   GET  /                   dashboard page
//   GET  /api/status         counters, live values, configuration, controls
//   GET  /api/constellation  recent equalised payload symbols + EQ taps
//   GET  /api/spectrum       averaged input spectrum (dB, -Fs/2..Fs/2)
//   GET  /api/history        per-interval trend points
//   POST /api/control        {"freq":Hz} {"gain":dB} (USRP), {"esn0":dB} {"cfo":Hz} (sim)
//
// POST requires the header `X-AIW-Control: 1` (a cross-site page cannot set
// it without a CORS preflight, which is never granted), and when bound to
// loopback the Host header must name localhost (DNS-rebinding guard).
#pragma once

#include <chrono>
#include <string>
#include <string_view>

#include "http_server.hpp"
#include "receiver.hpp"
#include "sample_source.hpp"
#include "ui_state.hpp"

namespace aiw {

std::string_view dashboard_page();  // generated from web/index.html

class Dashboard {
public:
    Dashboard(const Receiver& rx, SampleSource& src, UiState& ui, std::string bind_addr, std::string golden_name);
    HttpResponse handle(const HttpRequest& req);

private:
    HttpResponse status();
    HttpResponse constellation();
    HttpResponse spectrum();
    HttpResponse history(const HttpRequest& req);
    HttpResponse control(const HttpRequest& req);

    const Receiver& rx_;
    SampleSource& src_;
    UiState& ui_;
    std::string bind_addr_;
    std::string golden_name_;
    std::chrono::steady_clock::time_point t0_ = std::chrono::steady_clock::now();
};

}  // namespace aiw
