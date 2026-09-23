# AIW-Rx Installation Guide

**Applies to:** `aiw_receiver/` on `master`

**Companion documents:** [User manual](USER_MANUAL.md) · [Architecture](ARCHITECTURE.md)

This guide covers four ways to get a working `aiw_rx`:

| Route | Platform | USRP support | Effort |
| --- | --- | --- | --- |
| [A. Prebuilt binary](#a-prebuilt-binary-ubuntu-2404) | Ubuntu 24.04+ x86-64 | Yes | Minutes |
| [B. Build from source](#b-build-from-source-on-linux) | Any recent Linux | Yes, if UHD is installed | ~10 minutes |
| [C. Prebuilt Windows binary](#c-prebuilt-windows-binary) | Windows 10/11 x86-64 | **No** (simulator and files only) | Minutes |
| [D. Build on Windows with Visual Studio](#d-build-on-windows-with-visual-studio-usrp-support) | Windows 10/11 x86-64 | Yes | ~1 hour, untested |

Then continue with [Set up the USRP](#e-set-up-the-usrp) and [Verify the installation](#f-verify-the-installation).

---

## Requirements

| Item | Minimum | Notes |
| --- | --- | --- |
| CPU | 64-bit x86, 2 cores | 4 cores recommended; the receiver runs five worker threads |
| RAM | 256 MB free | The receiver itself uses about 25 MB |
| OS | Ubuntu 24.04 LTS (prebuilt) · any Linux with GCC ≥ 11 or Clang ≥ 14 (source) · Windows 10/11 | |
| Radio | Ettus USRP supported by UHD 4.x (configured default: serial `3273A14`, antenna RX2) | Not needed for the simulator or for file playback |
| Connection | USB 3.0 port (B2xx series) or Gigabit Ethernet (N2xx/X3xx series) | USB 2.0 works at 1.4 MSps but has less margin |
| Browser | Any current Chrome, Edge, Firefox or Safari | Only for the `--ui` dashboard |

---

## A. Prebuilt binary (Ubuntu 24.04)

The package `aiw_rx-ubuntu24.04-x86_64.tar.gz` contains `bin/aiw_rx`, `bin/aiw_tests`,
`INSTALL.txt` and `README.md`. It was built with GCC 13 against UHD 4.6.0 and generic x86-64
code, so it runs on any 64-bit Intel or AMD PC with Ubuntu 24.04 or newer (glibc 2.38+).

1. Unpack it wherever you like:

   ```sh
   tar xzf aiw_rx-ubuntu24.04-x86_64.tar.gz
   cd aiw_rx-ubuntu24.04-x86_64
   ```

2. Install the runtime libraries:

   ```sh
   sudo apt update
   sudo apt install libuhd4.6.0t64 uhd-host
   ```

3. Check that it runs:

   ```sh
   ./bin/aiw_tests          # ends with "... checks, 0 failures"
   ./bin/aiw_rx --help
   ```

4. Optionally, put it on your `PATH`:

   ```sh
   sudo install -m 755 bin/aiw_rx /usr/local/bin/aiw_rx
   ```

On Ubuntu 22.04 or older the binary fails with a `GLIBC_2.38 not found` error. Use route B there.

Continue with [Set up the USRP](#e-set-up-the-usrp).

---

## B. Build from source on Linux

### B.1 Get the source

Either unpack the source archive `aiw_receiver.zip`, or clone the repository:

```sh
git clone https://github.com/peyam/qutip-lectures
cd qutip-lectures/aiw_receiver
```

### B.2 Install the build dependencies

Ubuntu / Debian:

```sh
sudo apt update
sudo apt install build-essential cmake libeigen3-dev
# For USRP support (strongly recommended):
sudo apt install libuhd-dev libboost-dev uhd-host
```

`libboost-dev` is required together with `libuhd-dev`: the UHD headers include Boost headers,
and on Ubuntu 24.04 `libuhd-dev` does not always install them. Without it, the build stops with
`fatal error: boost/config.hpp: No such file or directory`.

Other distributions need the equivalent packages: a C++20 compiler, CMake ≥ 3.20, Eigen 3.3+,
and UHD 4.x development files with Boost headers. On Fedora, for example:
`sudo dnf install gcc-c++ cmake eigen3-devel uhd-devel boost-devel`.

### B.3 Configure and build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

CMake reports `UHD found: building with USRP support` or `UHD not found: aiw_rx will support
--source file|sim only`. To make a missing UHD a hard error, add `-DAIW_REQUIRE_UHD=ON`.

| CMake option | Default | Use |
| --- | --- | --- |
| `AIW_NATIVE` | `ON` | Optimise for this machine's CPU. Set `OFF` to build a binary you will copy to other machines |
| `AIW_FAST_MATH` | `ON` | `-ffast-math` |
| `AIW_REQUIRE_UHD` | `OFF` | Fail if UHD is not found |
| `AIW_BUILD_TESTS` | `ON` | Build `aiw_tests` and register CTest tests |

A **portable** build (what the prebuilt package uses):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAIW_NATIVE=OFF -DAIW_REQUIRE_UHD=ON
cmake --build build -j
```

### B.4 Test and install

```sh
ctest --test-dir build                          # 2 tests, both must pass
sudo install -m 755 build/aiw_rx /usr/local/bin/aiw_rx
```

The results are `build/aiw_rx` (receiver) and `build/aiw_tests` (self-test). The dashboard page
is compiled into `aiw_rx`, so nothing else needs installing.

---

## C. Prebuilt Windows binary

The package `aiw_rx-windows-x86_64.zip` contains `bin\aiw_rx.exe`, `bin\aiw_tests.exe`,
`INSTALL.txt` and `README.md`. The programs are statically linked and need no installer and no
extra DLLs.

> **Limitation:** this build cannot use a USRP. It runs the built-in simulator
> (`--source sim`) and decodes recorded capture files (`--source file`). For live reception on
> Windows, use [route D](#d-build-on-windows-with-visual-studio-usrp-support), or run on Linux.

1. Right-click the zip → **Extract All…**
2. Open **Command Prompt** or **PowerShell** in the extracted folder.
3. Run the self-test:

   ```bat
   bin\aiw_tests.exe
   ```

The files are not code-signed, so Windows SmartScreen may warn about them. Choose
**More info → Run anyway**. The dashboard only listens on `localhost`, so a Windows Firewall
prompt, if one appears, can be cancelled.

---

## D. Build on Windows with Visual Studio (USRP support)

> This route is supported by the build files but **has not been tested**. Please report problems.

1. Install **Visual Studio 2022** with the *Desktop development with C++* workload, and
   **CMake** (bundled with Visual Studio) and **Git**.
2. Install **UHD for Windows** (4.x, the MSVC build) from Ettus Research, then run the following
   in a new command prompt:

   ```bat
   uhd_images_downloader
   uhd_find_devices
   ```

   For USB devices, also install the UHD USB driver that the UHD installer provides.
3. Install **Eigen** and the **Boost headers** that match your UHD version, for example with
   vcpkg:

   ```bat
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   C:\vcpkg\vcpkg install eigen3 boost-config boost-format boost-optional --triplet x64-windows
   ```

4. From an **x64 Native Tools Command Prompt for VS 2022**, in `aiw_receiver`:

   ```bat
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DAIW_REQUIRE_UHD=ON ^
         -DUHD_DIR="C:/Program Files/UHD/lib/cmake/uhd" ^
         -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
   cmake --build build --config Release
   ctest --test-dir build -C Release
   ```

5. Add `C:\Program Files\UHD\bin` (where `uhd.dll` lives) to `PATH`, then run
   `build\Release\aiw_rx.exe`.

### Cross-compiling the Windows binary from Linux

This is how the prebuilt Windows package is made (no USRP support):

```sh
sudo apt install g++-mingw-w64-x86-64-posix libeigen3-dev
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
      -DAIW_NATIVE=OFF -DEigen3_DIR=/usr/share/eigen3/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j
```

---

## E. Set up the USRP

Skip this section if you only use the simulator or capture files.

1. **Download the FPGA and firmware images** (once per UHD installation):

   ```sh
   sudo uhd_images_downloader
   ```

2. **Linux, USB devices only: allow non-root access.** The Ubuntu `uhd-host` package installs
   the udev rule for you (`/usr/lib/udev/rules.d/60-uhd-host.rules`); just unplug and replug the
   USRP once after installing it. If you built UHD from source instead, install the rule manually:

   ```sh
   sudo cp /usr/libexec/uhd/utils/uhd-usrp.rules /etc/udev/rules.d/
   sudo udevadm control --reload-rules && sudo udevadm trigger
   ```

3. **Linux, Ethernet devices only:** give the host NIC a static address on the USRP's subnet
   (the factory default is `192.168.10.1`, so use e.g. `192.168.10.2/24`), and raise the socket
   buffers:

   ```sh
   sudo sysctl -w net.core.rmem_max=33554432 net.core.wmem_max=33554432
   ```

4. **Find the device:**

   ```sh
   uhd_find_devices
   ```

   The listing should include `serial: 3273A14`. If your unit has a different serial, pass it
   with `--args serial=<yours>` (see the [User manual](USER_MANUAL.md#4-command-line-reference)).

5. **Check streaming at the receiver's rate** (optional but recommended):

   ```sh
   /usr/libexec/uhd/examples/benchmark_rate --args serial=3273A14 --rx_rate 1.4e6 --duration 30
   ```

   The report should show zero overflows and zero dropped samples.

6. **Performance settings** (recommended for long runs):

   ```sh
   sudo cpupower frequency-set -g performance      # or set the governor in your BIOS/OS
   ```

---

## F. Verify the installation

Run these checks in order. Each one exercises more of the system than the last.

| # | Command | Expected result |
| --- | --- | --- |
| 1 | `aiw_tests` | Last line `2408 checks, 0 failures` (the count may grow in later versions) |
| 2 | `aiw_rx --source sim --sim-seconds 3 --expect-ber0` | Summary shows `post-FEC BER 0`; exit code 0 |
| 3 | `aiw_rx --source sim --ui` then open <http://localhost:8080> | Dashboard shows **Receiving**, about 700 frames/s |
| 4 | `aiw_rx --duration 10` with the transmitter on | Frames decoded, `USRP overflows 0`, `dropped chunks 0` |

If check 4 fails, see [Troubleshooting](USER_MANUAL.md#10-troubleshooting) in the user manual.

---

## G. Uninstall

* **Prebuilt or source install:** `sudo rm /usr/local/bin/aiw_rx`, then delete the unpacked
  folder or `build/` directory.
* **Libraries:** `sudo apt remove libuhd4.6.0t64 uhd-host` (and `libuhd-dev libboost-dev
  libeigen3-dev` if you built from source), if nothing else needs them.
* **Windows:** delete the extracted folder.

AIW-Rx writes no configuration files, registry entries or caches. The only file it creates is
the CSV you name with `--csv`.
