// Direct UHD interface (spec 7).  Compiled only when UHD is available
// (AIW_HAVE_UHD); everything else in the receiver is hardware-independent.
#pragma once

#ifdef AIW_HAVE_UHD

#include <uhd/stream.hpp>
#include <uhd/usrp/multi_usrp.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "config.hpp"
#include "sample_source.hpp"

namespace aiw {

struct UsrpConfig {
    std::string device_args = "serial=3273A14";
    std::string subdev;          // e.g. "A:A"; empty keeps the device default
    std::string antenna = "RX2";
    double sample_rate = SAMPLE_RATE;
    double center_freq = RX_CENTER_FREQ;
    double gain = RF_GAIN_RX;
};

inline uhd::usrp::multi_usrp::sptr initialize_usrp(const UsrpConfig& cfg) {
    auto usrp = uhd::usrp::multi_usrp::make(cfg.device_args);
    if (!cfg.subdev.empty()) usrp->set_rx_subdev_spec(uhd::usrp::subdev_spec_t(cfg.subdev));
    usrp->set_rx_rate(cfg.sample_rate);
    usrp->set_rx_freq(uhd::tune_request_t(cfg.center_freq));
    usrp->set_rx_gain(cfg.gain);
    usrp->set_rx_antenna(cfg.antenna);
    return usrp;
}

class UsrpSource final : public SampleSource {
public:
    explicit UsrpSource(const UsrpConfig& cfg) : cfg_(cfg) {
        usrp_ = initialize_usrp(cfg);
        std::cerr << "[usrp] " << usrp_->get_pp_string() << "\n"
                  << "[usrp] rate " << usrp_->get_rx_rate() / 1e6 << " MSps, freq "
                  << usrp_->get_rx_freq() / 1e6 << " MHz, gain " << usrp_->get_rx_gain() << " dB, antenna "
                  << usrp_->get_rx_antenna() << "\n";
        if (std::abs(usrp_->get_rx_rate() - cfg.sample_rate) > 1.0)
            std::cerr << "[usrp] WARNING: actual rate differs from requested rate\n";

        // Wait for the LO to lock (sensor name is common to all Ettus devices).
        const auto sensors = usrp_->get_rx_sensor_names(0);
        if (std::find(sensors.begin(), sensors.end(), "lo_locked") != sensors.end()) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            while (!usrp_->get_rx_sensor("lo_locked", 0).to_bool() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::cerr << "[usrp] LO " << (usrp_->get_rx_sensor("lo_locked", 0).to_bool() ? "locked" : "NOT locked")
                      << "\n";
        }

        uhd::stream_args_t args("fc32", "sc16");
        args.channels = {0};
        rx_ = usrp_->get_rx_stream(args);
    }

    ~UsrpSource() override { stop(); }

    void start() override {
        uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        cmd.stream_now = true;
        rx_->issue_stream_cmd(cmd);
        streaming_ = true;
    }

    void stop() override {
        if (!streaming_) return;
        rx_->issue_stream_cmd(uhd::stream_cmd_t(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS));
        streaming_ = false;
    }

    std::size_t read(cf32* out, std::size_t max) override {
        uhd::rx_metadata_t md;
        std::size_t got = 0;
        // Fill the whole chunk so downstream always sees full 8192-sample blocks.
        while (got < max) {
            const std::size_t n = rx_->recv(out + got, max - got, md, 0.5, true);
            switch (md.error_code) {
                case uhd::rx_metadata_t::ERROR_CODE_NONE:
                    break;
                case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
                    ++overflows_;  // 'O': samples lost inside the host/transport
                    break;
                case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
                    ++timeouts_;
                    if (!streaming_) return got;
                    break;
                default:
                    std::cerr << "[usrp] receive error: " << md.strerror() << "\n";
                    break;
            }
            got += n;
        }
        return got;
    }

    bool is_live() const override { return true; }
    std::size_t overflows() const override { return overflows_; }
    std::string describe() const override { return "usrp:" + cfg_.device_args; }

private:
    UsrpConfig cfg_;
    uhd::usrp::multi_usrp::sptr usrp_;
    uhd::rx_streamer::sptr rx_;
    std::atomic<bool> streaming_{false};
    std::atomic<std::size_t> overflows_{0};
    std::size_t timeouts_ = 0;
};

}  // namespace aiw

#endif  // AIW_HAVE_UHD
