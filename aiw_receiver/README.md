# AIW-Rx: standalone C++20 SDR receiver

A native replacement for the GNU Radio flowgraph `hwil_conventional_evaluation_rx.grc`:
256-QAM, RS(255,239)-shortened, UW-framed receiver at 1.4 MSps / 4 sps that talks to an
Ettus USRP through the UHD C++ API. It has no GNU Radio or Python runtime dependency.

## Build

```sh
sudo apt install cmake g++ libeigen3-dev
sudo apt install libuhd-dev libboost-dev      # optional: USRP support, see below
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build                         # offline verification
```

| CMake option | Default | Meaning |
| --- | --- | --- |
| `AIW_REQUIRE_UHD` | `OFF` | Fail configuration if UHD is missing. Without UHD, `aiw_rx` still builds with `--source file` and `--source sim`. |
| `AIW_NATIVE` | `ON` | `-march=native` |
| `AIW_FAST_MATH` | `ON` | `-ffast-math` |
| `AIW_BUILD_TESTS` | `ON` | `aiw_tests` plus a CTest end-to-end run of `aiw_rx` |

`libboost-dev` is needed alongside `libuhd-dev` because the UHD headers include Boost headers
(`boost/config.hpp`), and on Ubuntu 24.04 `libuhd-dev` does not always pull them in. Without it the
build fails with `fatal error: boost/config.hpp: No such file or directory`.

For a binary that runs on other x86-64 machines, turn off CPU-specific tuning:
`cmake -S . -B build -DAIW_NATIVE=OFF -DAIW_REQUIRE_UHD=ON`. At run time such a binary only needs
`sudo apt install libuhd4.6.0t64 uhd-host` (Ubuntu 24.04) and a one-time `sudo uhd_images_downloader`.

## Run

```sh
./build/aiw_rx                                        # USRP serial=3273A14, 917 MHz, 45 dB
./build/aiw_rx --freq 915e6 --gain 40 --csv run.csv   # per-second metrics to CSV
./build/aiw_rx --source file --file capture.fc32      # raw interleaved complex float32 capture
./build/aiw_rx --source sim --realtime                # built-in transmitter and channel model
./build/aiw_rx --help
```

Every report interval (1 s by default), `aiw_rx` prints frames decoded, frames with exact payload,
uncorrectable frames, RS symbols corrected (and the per-block maximum), pre-FEC and post-FEC BER,
radiometric SNR, UW-aided MER, estimated CFO, and mean/max latency. It also flags dropped chunks and
USRP `O` overflows. `--csv` writes the same data plus AGC gain, timing-loop rate and equalizer
condition number.

## Architecture

```
T1 radio ingestion ──SampleChunk(8192)──▶ T2 DC block → mix −300 kHz + 93-tap LPF → AGC → PFB clock sync ──SymbolChunk──▶
T3 UW correlator → segment extraction → two-pass (CFO + 5-tap LLS) equalizer → demux ──Frame──▶
T4 256-QAM slicer → RS decode (batches of 8) → BER vs. ground truth
T2 ──DC-blocked copy──▶ T5 SNR radiometer          main thread: metrics dispatcher / CSV
```

All inter-thread queues are lock-free SPSC rings (`include/circular_buffer.hpp`) with pre-allocated
slots that are filled in place. A live USRP source is never back-pressured: if a queue is full, the
chunk is dropped and counted. File and sim sources block instead, so no data is lost.

| File | Contents |
| --- | --- |
| `include/config.hpp` | Spec constants (section 2) and `RxConfig` runtime parameters |
| `include/dc_blocker.hpp`, `agc.hpp`, `filters.hpp` | Sections 4.1–4.3: DC notch, AGC, filter design, NCO, frequency-translating FIR |
| `include/pfb_clock_sync.hpp`, `src/pfb_clock_sync.cpp` | Section 4.4: port of GNU Radio `pfb_clock_sync_ccf` |
| `include/sync_correlator.hpp`, `src/sync_correlator.cpp` | Section 5.2: normalized UW correlator and segment extraction |
| `include/equalizer.hpp`, `src/equalizer.cpp` | Section 5.3: two-pass CFO/phase and Tikhonov LLS equalizer (Eigen) |
| `include/constellation.hpp` | Section 6.1: 256-QAM map and slice |
| `include/fec_reed_solomon.hpp`, `src/fec_reed_solomon.cpp` | Section 6.2: RS(255,239) over GF(2^8)/0x11D, BM + Chien + Forney, shortening |
| `include/frame_decoder.hpp` | Section 6.3: slicer + RS + pre/post-FEC BER |
| `include/snr_estimator.hpp` | Section 6.4: radiometric SNR |
| `include/usrp_source.hpp`, `sample_source.hpp` | Section 7: UHD streaming; file and simulator sources |
| `include/tx_simulator.hpp`, `src/tx_simulator.cpp` | Reference transmitter and channel (CFO, clock ppm, echo, DC, AWGN) |
| `src/receiver.cpp` | Thread pipeline and metrics |

## Deviations from the specification

Each of the following was found while implementing or verifying the spec. Each one can be switched
back to the literal spec behavior from the command line.

1. **The unique word has 143 symbols, not 136.** The table in section 5.1 has 143 entries. It is
   exactly the Zadoff–Chu sequence `exp(-jπ·25·n(n+1)/143)` (largest difference 6e-7, which is why
   it is palindromic). Frame geometry is derived from the UW actually in use, so the default segment
   is 143 + 216 + 143 = **502 symbols**, and the trailing-UW check is at n0 + 359. If the transmitter
   really uses a 136-symbol UW, select it with `--uw-zc 136:<root>`.
2. **The `GOLDEN_PAYLOAD` hex table does not match NumPy.**
   `np.random.default_rng(42).integers(0,256,200,dtype=np.uint8)` produces `88 26 d9 16 …`, but the
   spec's table starts `2c f6 d0 ef …` and matches none of the usual NumPy or Python generators
   either. The default is the real NumPy output (`tools/gen_golden_payload.py`). Use
   `--golden spec` for the spec's table, or `--golden-file` for any 200-byte file.
3. **The slicer's rounding formula is wrong.** `2⌊(x+1)/2⌋−1` maps 0.5 to −1 and 2.5 to 1. The
   nearest odd integer is `2⌊x/2⌋+1`, which is what the slicer implements.
4. **The matched filter is not applied twice.** The PFB prototype *is* the RRC matched filter
   (32 branches × 64 taps, as in GNU Radio). A separate 64-tap RRC in front of it would produce a
   raised-cosine-squared pulse with ISI.
5. **AGC rates default to 1e-3/1e-3.** The spec's attack 0.1 / decay 0.001 with a multiplicative
   per-sample update modulates the gain inside a frame. In simulation this caps MER at about 23 dB
   and makes every 256-QAM frame uncorrectable, even with no noise. The update equation is the
   spec's; only the default rates differ (`--agc-attack 0.1 --agc-decay 0.001` restores them).
6. **PFB damping defaults to 2·nfilts = 64.** That is the value GNU Radio's `pfb_clock_sync` sets
   internally (the GRC block does not expose it). The spec's ζ = 0.707 (`--damping 0.707`) also
   works, but it takes about 5× longer to converge.
7. **The equalizer taps are centered by default** (decision delay 2). The causal form in section 5.3
   (`--eq-delay 0`) cannot remove pre-cursor ISI. On a synthetic channel with a 0.1 pre-cursor it
   reaches 18.8 dB MER, against 48.5 dB when centered.
8. **The "two-pass" correction includes CFO estimation.** The chain has no carrier-tracking loop, so
   pass 1 estimates frequency from the two UWs (coarse: lag-32 inside each UW, ±5.4 kHz range; fine:
   lead-to-trail phase over 359 symbols) and de-rotates the segment. Pass 2 is the LLS equalizer plus
   residual phase removal. `--no-cfo` disables pass 1.
9. **The subdevice defaults to the device's own.** The spec's `"0:A"` is not valid UHD subdev
   syntax; pass `--subdev A:A` (B2xx) or similar if needed.
10. **The first 16 frames are excluded from BER** (`--warmup-frames`), about 22 ms while the AGC and
    timing loop converge. They are counted and reported separately.

The RS code uses fcr = 0, generator α = 2, polynomial 0x11D (the DVB-T, gr-dtv and `reedsolo`
convention). The parity of `GOLDEN_PAYLOAD` is checked byte-for-byte against Python `reedsolo` in
the tests. Use `--rs-fcr` if the transmitter differs.

## Verification (spec section 9)

| Gate | How it is checked | Offline result |
| --- | --- | --- |
| 1. UHD ingestion without drops | `aiw_rx` reports `O` overflows (UHD metadata) and dropped chunks per interval | Needs hardware. The pipeline runs at about 10× real time on 4 cores (20 s of signal in 2.1 s, simulator included) |
| 2. DC notch and translation | `aiw_tests`: DC blocker kills a constant input; a +300 kHz tone comes out at 0 Hz; the −700 kHz image is rejected by 57 dB | pass |
| 3. 1 sample/symbol, open eye | Loopback symbol/sample ratio = 0.2500; MER 41 dB on a clean channel | pass |
| 4. UW peaks at segment spacing | Loopback checks every consecutive segment is exactly 502 (±1) symbols apart, and the trailing peak at +359 is required for detection | pass |
| 5. LLS conditioning and cluster tightening | Condition number logged per frame (≈1.4); MER 36 dB at Es/N0 38 dB with echo, 400 Hz CFO and 10 ppm | pass |
| 6. RS decoding, BER = 0 | Impaired loopback: BER 0 with max corrections < 8 per block. At 29 dB, pre-FEC BER 1.8e-3 is corrected to 0 | pass |

`--realtime` simulation (Es/N0 32 dB, 150 Hz CFO, 5 ppm, echo): 4167 frames, post-FEC BER 0, mean
latency 6.0 ms and maximum 16 ms, measured from ingestion of the chunk that completes a segment to
the end of its RS decode, including the 8-codeword FEC batch. Use `--fec-batch 1` for lower latency.
