#include "equalizer.hpp"

#include <Eigen/Dense>
#include <cmath>

namespace aiw {

namespace {
using cd = std::complex<double>;
constexpr std::size_t COARSE_LAG = 32;
}  // namespace

TwoPassEqualizer::TwoPassEqualizer(std::vector<cf32> uw, std::size_t data_len, int delay, double lambda,
                                   bool cfo_correction)
    : uw_(std::move(uw)), L_(uw_.size()), data_len_(data_len), delay_(delay), lambda_(lambda),
      cfo_(cfo_correction) {}

bool TwoPassEqualizer::process(const std::vector<cf32>& seg, EqualizerResult& out) {
    const std::size_t M = SEGMENT_MARGIN;
    const std::size_t core = 2 * L_ + data_len_;
    if (seg.size() != core + 2 * M || data_len_ != DATA_LENGTH_SYM) return false;
    if (delay_ < 0 || delay_ > static_cast<int>(M)) return false;

    r_.resize(seg.size());
    for (std::size_t i = 0; i < seg.size(); ++i) r_[i] = cd(seg[i].real(), seg[i].imag());
    // r(i) with i relative to the leading UW start (may be -M .. core+M-1).
    auto r = [&](long i) -> cd& { return r_[static_cast<std::size_t>(i + static_cast<long>(M))]; };
    const long trail = static_cast<long>(L_ + data_len_);
    auto u = [&](std::size_t k) { return cd(uw_[k].real(), uw_[k].imag()); };

    // ---------------- Pass 1: frequency / phase -----------------------------
    double w = 0.0;
    if (cfo_) {
        cd acc{};
        for (long base : {0L, trail}) {
            for (std::size_t k = 0; k + COARSE_LAG < L_; ++k) {
                const cd z0 = r(base + static_cast<long>(k)) * std::conj(u(k));
                const cd z1 = r(base + static_cast<long>(k + COARSE_LAG)) * std::conj(u(k + COARSE_LAG));
                acc += z1 * std::conj(z0);
            }
        }
        const double w_coarse = std::arg(acc) / static_cast<double>(COARSE_LAG);

        cd c_lead{}, c_trail{};
        for (std::size_t k = 0; k < L_; ++k) {
            const double kk = static_cast<double>(k);
            c_lead += r(static_cast<long>(k)) * std::conj(u(k)) * std::polar(1.0, -w_coarse * kk);
            c_trail += r(trail + static_cast<long>(k)) * std::conj(u(k)) *
                       std::polar(1.0, -w_coarse * (kk + static_cast<double>(trail)));
        }
        w = w_coarse + std::arg(c_trail * std::conj(c_lead)) / static_cast<double>(trail);

        for (long i = -static_cast<long>(M); i < static_cast<long>(core + M); ++i)
            r(i) *= std::polar(1.0, -w * static_cast<double>(i));
    }
    out.cfo_rad_per_sym = w;

    // ---------------- Pass 2: LLS equaliser ---------------------------------
    constexpr int P = EQ_TAPS;
    const Eigen::Index rows = static_cast<Eigen::Index>(2 * L_);
    Eigen::MatrixXcd Y(rows, P);
    Eigen::VectorXcd d(rows);
    Eigen::Index row = 0;
    for (long base : {0L, trail}) {
        for (std::size_t k = 0; k < L_; ++k, ++row) {
            const long n = base + static_cast<long>(k) + delay_;
            for (int l = 0; l < P; ++l) Y(row, l) = r(n - l);
            d(row) = u(k);
        }
    }
    Eigen::MatrixXcd A = Y.adjoint() * Y;
    A.diagonal().array() += lambda_;
    const Eigen::VectorXcd b = Y.adjoint() * d;
    const Eigen::VectorXcd wv = A.ldlt().solve(b);
    if (!wv.allFinite()) return false;

    const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXcd> es(A, Eigen::EigenvaluesOnly);
    const auto ev = es.eigenvalues();
    out.cond_estimate = ev.minCoeff() > 0 ? ev.maxCoeff() / ev.minCoeff() : INFINITY;

    auto eq = [&](long n) {
        cd s{};
        for (int l = 0; l < P; ++l) s += wv(l) * r(n + delay_ - l);
        return s;
    };

    // Residual phase and MER over the equalised UWs.
    cd rot{};
    double err = 0.0, ref = 0.0;
    std::vector<cd> uw_hat;
    uw_hat.reserve(2 * L_);
    for (long base : {0L, trail})
        for (std::size_t k = 0; k < L_; ++k) {
            const cd s = eq(base + static_cast<long>(k));
            uw_hat.push_back(s);
            rot += s * std::conj(u(k));
        }
    const double theta = std::arg(rot);
    const cd derot = std::polar(1.0, -theta);
    for (std::size_t i = 0; i < uw_hat.size(); ++i) {
        const cd e = uw_hat[i] * derot - u(i % L_);
        err += std::norm(e);
        ref += std::norm(u(i % L_));
    }
    out.residual_phase = theta;
    out.uw_mer_db = 10.0 * std::log10(ref / std::max(err, 1e-30));

    for (std::size_t i = 0; i < data_len_; ++i) {
        const cd s = eq(static_cast<long>(L_ + i)) * derot;
        out.data[i] = cf32(static_cast<float>(s.real()), static_cast<float>(s.imag()));
    }
    for (int l = 0; l < P; ++l)
        out.taps[static_cast<std::size_t>(l)] = cf32(static_cast<float>(wv(l).real()), static_cast<float>(wv(l).imag()));
    return true;
}

}  // namespace aiw
