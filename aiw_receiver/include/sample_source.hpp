// Sample sources feeding Thread 1 (radio ingestion).
#pragma once

#include <atomic>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

#include "config.hpp"
#include "tx_simulator.hpp"

namespace aiw {

class SampleSource {
public:
    virtual ~SampleSource() = default;
    virtual void start() {}
    virtual void stop() {}
    // Reads up to `max` samples.  Returns 0 at end of stream.
    virtual std::size_t read(cf32* out, std::size_t max) = 0;
    // A live source cannot be back-pressured: if the pipeline falls behind
    // its chunks are dropped (and counted).  Offline sources block instead.
    virtual bool is_live() const { return false; }
    virtual std::size_t overflows() const { return 0; }
    virtual std::string describe() const = 0;
};

// Raw interleaved complex float32 (GNU Radio file sink / uhd rx_samples_to_file fc32).
class FileSource final : public SampleSource {
public:
    FileSource(const std::string& path, bool loop) : path_(path), loop_(loop) {
        f_ = std::fopen(path.c_str(), "rb");
        if (!f_) throw std::runtime_error("cannot open " + path);
    }
    ~FileSource() override {
        if (f_) std::fclose(f_);
    }
    std::size_t read(cf32* out, std::size_t max) override {
        std::size_t n = std::fread(out, sizeof(cf32), max, f_);
        if (n == 0 && loop_) {
            std::rewind(f_);
            n = std::fread(out, sizeof(cf32), max, f_);
        }
        return n;
    }
    std::string describe() const override { return "file:" + path_ + (loop_ ? " (loop)" : ""); }

private:
    std::string path_;
    bool loop_;
    std::FILE* f_ = nullptr;
};

// Built-in transmitter + channel model; `total` = 0 means unbounded.
class SimSource final : public SampleSource {
public:
    SimSource(std::unique_ptr<TxSimulator> tx, std::size_t total) : tx_(std::move(tx)), total_(total) {}
    std::size_t read(cf32* out, std::size_t max) override {
        std::size_t n = max;
        if (total_) {
            if (produced_ >= total_) return 0;
            n = std::min(max, total_ - produced_);
        }
        tx_->generate(out, n);
        produced_ += n;
        return n;
    }
    std::string describe() const override { return "simulated transmitter"; }

private:
    std::unique_ptr<TxSimulator> tx_;
    std::size_t total_;
    std::size_t produced_ = 0;
};

}  // namespace aiw
