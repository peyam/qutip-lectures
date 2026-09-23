#include "fec_reed_solomon.hpp"

#include <algorithm>

namespace aiw {

ReedSolomon::ReedSolomon(int fcr, unsigned prim_poly) : fcr_(fcr) {
    unsigned x = 1;
    for (int i = 0; i < 255; ++i) {
        exp_[static_cast<std::size_t>(i)] = static_cast<uint8_t>(x);
        log_[x] = i;
        x <<= 1;
        if (x & 0x100u) x ^= prim_poly;
    }
    for (int i = 255; i < 512; ++i) exp_[static_cast<std::size_t>(i)] = exp_[static_cast<std::size_t>(i - 255)];
    log_[0] = 0;  // unused; guarded by callers

    // g(x) = prod_{i=0}^{15} (x - alpha^(fcr+i))
    gen_.fill(0);
    gen_[0] = 1;
    for (int i = 0; i < NROOTS; ++i) {
        const uint8_t root = gf_pow_alpha(fcr_ + i);
        for (int j = i + 1; j >= 1; --j)
            gen_[static_cast<std::size_t>(j)] =
                static_cast<uint8_t>(gen_[static_cast<std::size_t>(j - 1)] ^ gf_mul(gen_[static_cast<std::size_t>(j)], root));
        gen_[0] = gf_mul(gen_[0], root);
    }
}

void ReedSolomon::encode(std::span<const uint8_t, K> msg, std::span<uint8_t, NROOTS> parity) const {
    // LFSR division; parity[0] is the highest-order remainder coefficient,
    // transmitted immediately after the message.
    std::array<uint8_t, NROOTS> r{};
    for (int i = 0; i < K; ++i) {
        const uint8_t fb = msg[static_cast<std::size_t>(i)] ^ r[0];
        for (int j = 0; j < NROOTS - 1; ++j)
            r[static_cast<std::size_t>(j)] =
                static_cast<uint8_t>(r[static_cast<std::size_t>(j + 1)] ^ gf_mul(fb, gen_[static_cast<std::size_t>(NROOTS - 1 - j)]));
        r[NROOTS - 1] = gf_mul(fb, gen_[0]);
    }
    std::copy(r.begin(), r.end(), parity.begin());
}

int ReedSolomon::decode(std::span<uint8_t, N> cw, int pad) const {
    // Codeword c(x) = sum cw[i] x^(N-1-i).
    std::array<uint8_t, NROOTS> synd{};
    bool any = false;
    for (int j = 0; j < NROOTS; ++j) {
        const uint8_t a = gf_pow_alpha(fcr_ + j);
        uint8_t s = 0;
        for (int i = 0; i < N; ++i) s = static_cast<uint8_t>(gf_mul(s, a) ^ cw[static_cast<std::size_t>(i)]);
        synd[static_cast<std::size_t>(j)] = s;
        any |= (s != 0);
    }
    if (!any) return 0;

    // Berlekamp-Massey: error locator Lambda(x), Lambda[0] = 1.
    std::array<uint8_t, NROOTS + 1> lambda{}, prev{}, tmp{};
    lambda[0] = 1;
    prev[0] = 1;
    int L = 0, m = 1;
    uint8_t b = 1;
    for (int n = 0; n < NROOTS; ++n) {
        uint8_t d = synd[static_cast<std::size_t>(n)];
        for (int i = 1; i <= L; ++i)
            d ^= gf_mul(lambda[static_cast<std::size_t>(i)], synd[static_cast<std::size_t>(n - i)]);
        if (d == 0) {
            ++m;
            continue;
        }
        const uint8_t coef = gf_mul(d, gf_inv(b));
        tmp = lambda;
        for (int i = m; i <= NROOTS; ++i)
            lambda[static_cast<std::size_t>(i)] ^= gf_mul(coef, prev[static_cast<std::size_t>(i - m)]);
        if (2 * L <= n) {
            L = n + 1 - L;
            prev = tmp;
            b = d;
            m = 1;
        } else {
            ++m;
        }
    }
    if (L > T) return -1;

    // Chien search.  Position p (index into cw) has locator X = alpha^(N-1-p);
    // Lambda(X^-1) = 0 at an error.
    std::array<int, T> pos{};
    int nroots = 0;
    for (int p = 0; p < N; ++p) {
        const int e = N - 1 - p;
        uint8_t v = 0;
        for (int i = 0; i <= L; ++i)
            v ^= gf_mul(lambda[static_cast<std::size_t>(i)], gf_pow_alpha(-e * i));
        if (v == 0) {
            if (nroots == T) return -1;
            pos[static_cast<std::size_t>(nroots++)] = p;
        }
    }
    if (nroots != L) return -1;

    // Omega(x) = S(x) Lambda(x) mod x^16, S(x) = sum synd[j] x^j.
    std::array<uint8_t, NROOTS> omega{};
    for (int i = 0; i < NROOTS; ++i) {
        uint8_t v = 0;
        for (int j = 0; j <= std::min(i, L); ++j)
            v ^= gf_mul(lambda[static_cast<std::size_t>(j)], synd[static_cast<std::size_t>(i - j)]);
        omega[static_cast<std::size_t>(i)] = v;
    }

    // Forney: e = X^(1-fcr) * Omega(X^-1) / Lambda'(X^-1).
    for (int r = 0; r < nroots; ++r) {
        const int p = pos[static_cast<std::size_t>(r)];
        if (p < pad) return -1;
        const int e = N - 1 - p;
        const uint8_t xinv = gf_pow_alpha(-e);
        uint8_t om = 0, xp = 1;
        for (int i = 0; i < NROOTS; ++i) {
            om ^= gf_mul(omega[static_cast<std::size_t>(i)], xp);
            xp = gf_mul(xp, xinv);
        }
        // Formal derivative keeps odd-power terms: Lambda'(x) = sum_{i odd} lambda_i x^(i-1).
        uint8_t dl = 0;
        for (int i = 1; i <= L; i += 2)
            dl ^= gf_mul(lambda[static_cast<std::size_t>(i)], gf_pow_alpha(-e * (i - 1)));
        if (dl == 0) return -1;
        const uint8_t mag = gf_mul(gf_mul(om, gf_inv(dl)), gf_pow_alpha(e * (1 - fcr_)));
        cw[static_cast<std::size_t>(p)] ^= mag;
    }
    return nroots;
}

void ReedSolomon::encode_shortened(std::span<const uint8_t, SHORT_PAYLOAD> payload,
                                   std::span<uint8_t, SHORT_CW> codeword) const {
    std::array<uint8_t, K> msg{};
    std::copy(payload.begin(), payload.end(), msg.begin() + PAD);
    std::copy(payload.begin(), payload.end(), codeword.begin());
    encode(msg, codeword.subspan<SHORT_PAYLOAD, NROOTS>());
}

int ReedSolomon::decode_shortened(std::span<uint8_t, SHORT_CW> codeword) const {
    std::array<uint8_t, N> full{};
    std::copy(codeword.begin(), codeword.end(), full.begin() + PAD);
    const int r = decode(full, PAD);
    if (r >= 0) std::copy(full.begin() + PAD, full.end(), codeword.begin());
    return r;
}

}  // namespace aiw
