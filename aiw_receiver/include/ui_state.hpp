// Data shared between the receiver threads and the web dashboard.
//
// Producers on the real-time path (T3, T5) publish with try_lock and simply
// skip an update if the web server happens to hold the lock, so the
// dashboard can never stall signal processing.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <mutex>
#include <vector>

#include "config.hpp"

namespace aiw {

struct HistoryPoint {
    double t = 0.0;
    double frames_per_s = 0.0;
    double pre_ber = NAN, post_ber = NAN;
    double snr_db = NAN, mer_db = NAN, cfo_hz = NAN;
    double lat_mean_ms = NAN, lat_max_ms = NAN;
    double uncorrectable = 0.0, rs_max = 0.0;
};

struct UiState {
    static constexpr std::size_t CONSTELLATION_POINTS = 2048;
    static constexpr std::size_t HISTORY_POINTS = 600;

    std::mutex mu;
    // Equalised payload symbols (ring buffer) and latest equaliser taps.
    std::vector<cf32> constellation = std::vector<cf32>(CONSTELLATION_POINTS);
    std::size_t const_count = 0;
    std::size_t const_pos = 0;
    std::array<cf32, EQ_TAPS> taps{};
    // Input spectrum (DC-blocked, before translation), dB, fft-shifted.
    std::vector<float> psd_db;
    // One point per report interval.
    std::deque<HistoryPoint> history;
    bool run_finished = false;

    void publish_frame(const std::array<cf32, DATA_LENGTH_SYM>& data, const std::array<cf32, EQ_TAPS>& t) {
        std::unique_lock lk(mu, std::try_to_lock);
        if (!lk) return;
        for (const cf32& s : data) {
            constellation[const_pos] = s;
            const_pos = (const_pos + 1) % CONSTELLATION_POINTS;
        }
        const_count = std::min(CONSTELLATION_POINTS, const_count + data.size());
        taps = t;
    }

    void publish_psd(const std::vector<float>& p) {
        std::unique_lock lk(mu, std::try_to_lock);
        if (lk) psd_db = p;
    }

    void push_history(const HistoryPoint& h) {
        std::lock_guard lk(mu);
        history.push_back(h);
        while (history.size() > HISTORY_POINTS) history.pop_front();
    }
};

}  // namespace aiw
