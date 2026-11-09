# Linux x86_64 build and coverage

Linux shares the Warthog CPU hash, CUDA kernel, candidate queue, adaptive
filter, benchmark, calibration, monitoring and console renderer with Windows.
Platform code supplies POSIX sockets, dynamic library loading, CPU identity,
SIGINT/SIGTERM shutdown, terminal detection and size queries.

The paired AVX2/AES/PCLMUL hash compiles separately from baseline feature checks.
Older CPUs use the portable hash. Instructions are selected by capability,
without individual CPU or GPU model special cases. QEMU tests with Intel
Nehalem and AMD Opteron feature sets verify fallback correctness without AVX;
they do not establish physical Intel performance.

## Run a package

The Linux package is built on Ubuntu 24.04 x86_64 with Clang 18. It requires
glibc 2.39 or newer, a compatible C++ runtime and OpenSSL 3 (`libcrypto.so.3`).
Other distributions may require a native build. NVIDIA mining also requires a
working vendor driver and a separately installed NVRTC runtime. CUDA libraries
are not included. A normal Toolkit installation supplies the runtime; see
[CUDA setup](cuda-setup.md).

```sh
tar -xzf BRminer-1.2.0-linux-x64.tar.gz
cd BRminer-1.2.0-linux-x64
./brminer -a warthog -p YOUR-NODE:3456 -w YOUR-WALLET --cpu_threads 14
./benchmark-warthog.sh
./calibrate-warthog.sh --full
```

`mining-warthog.sh` has a public-pool example with a wallet placeholder. Ctrl+C
or SIGTERM stops mining. An interactive terminal shows a colored dashboard;
redirected output contains plain snapshots. See [calibration controls](benchmark-calibration.md).

## Mining console (Windows and Linux)

The console uses an htop-inspired layout with numeric hashrates beside their labels,
device telemetry, pool connection status, and timestamped events. The display
updates once per second using a rolling ten-second rate window. GPU utilization
is shown as a percentage in the device table. Temperature values turn yellow
at 75 C and red at 85 C; these are display thresholds, not hardware-specific
safe operating limits.

`JANUS (10s)` is the combined work rate; `GPU SHA256t` and `CPU Verus` are its
supporting stage rates. `AVG` is the average combined Janus rate since the session
started. These rates measure different stages and should not be added together.
Estimated share time is difficulty divided by the current Janus rate; it is a
statistical interval, not a countdown. The full layout fits terminals at least
80 columns wide and 20 rows tall; smaller panes use a compact numeric view.

Only changed rows are written, in a single buffered update. The dashboard fits
smaller panes, clips long event messages to one row, and restores the normal
terminal and cursor on exit. The device table shows available NVML readings;
missing sensors remain `N/A`. Per-GPU hashrates are not available on multi-GPU
rigs, so their total stays in the top GPU SHA256t panel. Repeated Stratum
difficulty messages appear only when the difficulty value changes.

In the classic Windows console, BRminer temporarily disables mouse Quick Edit
while showing the dashboard or plain logs. Quick Edit selection can block console
writes and hold up mining; ordinary clicks therefore must not start a selection.
Your original input settings are restored on exit, and Ctrl+C remains available.
This uses the Windows [console input mode API](https://learn.microsoft.com/en-us/windows/console/setconsolemode).
If an older build stops after a mouse click, press Enter or Esc to end the
selection, then restart with the updated executable.

Set a nonempty `NO_COLOR` value, such as `NO_COLOR=1`, to disable color while keeping the live dashboard. Set
`BRMINER_CONSOLE=plain` for timestamped logs and ten-second text snapshots.
Redirected output, `TERM=dumb`, diagnostic mode, and quiet benchmarks do not
emit dashboard escape sequences. The renderer uses ASCII text and standard
terminal colors for consistent Windows, SSH, and Linux font support.

For example, enable plain output before starting the existing mining launcher:

```powershell
# PowerShell
$env:BRMINER_CONSOLE = 'plain'
.\mining-warthog.bat
# Restore automatic terminal selection for the next launch:
Remove-Item Env:BRMINER_CONSOLE
```

```sh
# Linux: applies only to this invocation
BRMINER_CONSOLE=plain ./mining-warthog.sh
```

In Command Prompt, use `set "BRMINER_CONSOLE=plain"` before launching, and
`set "BRMINER_CONSOLE="` to restore automatic selection. The same environment
syntax works for `NO_COLOR`. Redirecting stdout also selects plain output;
quiet benchmarks suppress progress output entirely.

The visual reference is [htop](https://htop.dev/); the buffered frame comparison
follows the approach described in [Ratatui's rendering documentation](https://ratatui.rs/concepts/rendering/under-the-hood/).
No additional terminal library is required.

## Native build

On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install build-essential clang cmake ninja-build git ca-certificates \
  curl python3 libboost-dev libssl-dev pkg-config
cmake -S . -B out/build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build out/build/linux --parallel
ctest --test-dir out/build/linux --output-on-failure
```

CMake fetches pinned upstream sources. Set `BRMINER_NVRTC_DLL` to the full
`libnvrtc.so.13` path for CTest to require CUDA execution; keep matching builtins
alongside it. Without that variable the CUDA test explicitly skips. OpenCL
device execution needs an installed OpenCL runtime.

Build the reproducible container environment with
`docker build -t brminer-linux-builder:goal -f scripts/linux/Dockerfile .`.
The helper `build-in-container.sh` uses `/workspace` as a read-only checkout
mount and `/work` as its writable native build/dependency volume. It reuses
pinned source caches when available and fetches them on a fresh checkout.
See [complete build and Docker commands](build.md).

`scripts/linux/fetch-cuda.py 13.4.2 /work` and the same command with `12.9.1`
download NVRTC through NVIDIA's official manifests and verify SHA-256 before
extraction. These optional downloads are for the user's own installation. Run tests, then
use `scripts/linux/package-release.py --help` for packaging paths. The packager
runs in the Ubuntu build environment and excludes CUDA libraries.

## Verified scope

The November 9, 2026 development source passed 20 CTest checks on native Ubuntu
24.04 under Docker/WSL2. Console checks cover color and `NO_COLOR`, wide and
compact layouts, resizing, immediate share counters, cursor/terminal restoration,
and one-second refresh in a real miner process. The synthetic Stratum test also
checks that repeated difficulty values are silent while changed values are logged.
CPU benchmark smoke checks passed. CUDA and OpenCL device execution skipped in
this console-update test environment because their container runtimes were absent.

The earlier November 2 build executed real CUDA work under Docker Desktop/WSL2 on
the host's Ryzen 7 5800X and RTX 4060 using NVRTC 13.4.92. All 19 then-applicable
checks passed: known-answer/equivalence and fallback hashing, CUDA-versus-CPU
output, calibration cache policy, optional NVML, offline benchmark, terminal
resize/scrolling, and synthetic Stratum acceptance/rejection/reconnection with
graceful shutdown. OpenCL device execution skipped because the container lacks
an OpenCL runtime.

Those earlier real solo-node sessions completed over an hour without a mining error or rejected
submission, with controlled reconnection and clean shutdown verified. The final
extracted Linux package then completed five minutes on the user-authorized
HeroMiners pool: 14 accepted submissions, zero rejects and clean shutdown.
Two controlled five-minute default benchmark runs measured 472.80 and 470.25 MH/s
Janus after warmup. No solo block was found in these Linux node sessions.
See [the dated acceptance report](development-goal.md) for the corresponding build evidence.

WSL establishes Linux functionality on the available hardware, not bare-metal
Linux speed or coverage of every AMD/Intel CPU, NVIDIA generation, or multi-GPU
combination. Those physical configurations remain untested. The second Windows
PC has no remote command access; a manual benchmark launcher is included.

NVIDIA references:

- [NVRTC platforms and compilation output](https://docs.nvidia.com/cuda/nvrtc/)
- [Official CUDA redistributables](https://developer.download.nvidia.com/compute/cuda/redist/)
