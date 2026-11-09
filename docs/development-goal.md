# BRminer validation and acceptance reports

## November 9, 2026 development-console validation

The `dev/contribute` source adds a colored numeric dashboard with each hashrate
beside its label, one-second display refresh, rolling ten-second live rates,
device telemetry and timestamped severity events. Changed rows are buffered into
one write. `NO_COLOR` and plain-output modes are supported. Repeated difficulty
values no longer produce duplicate event lines.

Windows MSVC Release passed 18/18 CTest checks. Native Ubuntu 24.04/Clang 18 under
Docker/WSL2 passed 20/20 with QEMU and pyte installed. The console checks cover
numeric layouts, colors, `NO_COLOR`, resizing, immediate counters, terminal/cursor
restoration and a real miner process refreshing before the ten-second control
sample. The Stratum fixture checks first/changed difficulty announcements,
duplicate suppression, acceptance/rejection and reconnects. CPU benchmark smoke
checks also passed. Optional GPU test binaries explicitly skip execution when
their runtime is absent; Linux CUDA/OpenCL execution was not exercised in this
console-update environment. These changes do not add sustained performance or
public-pool measurements.

The Windows live renderer was also exercised in a native pseudoconsole. An
isolated hidden classic-console fixture verifies that mouse Quick Edit is
disabled during dashboard, plain-log and diagnostic output, Ctrl+C and the other
input flags are preserved, clicks and minimizing do not block refreshes, and
input/output modes are restored on exit. Quiet and redirected output leave the
input mode unchanged.
The reproducible checks are [console tests](../tests/warthog_console_tests.cpp),
[Windows console tests](../tests/windows_console_test.py),
[Linux terminal tests](../tests/linux_terminal_test.py),
[Stratum lifecycle tests](../tests/stratum_lifecycle_test.py) and
[release smoke verification](../scripts/verify-release.py).

The published [v1.1.0 release](https://github.com/JohnnyB813/BRminer/releases/tag/v1.1.0)
targets `b3b1ad9` and predates this console update. Current user instructions are
in [the build guide](build.md), [console guide](warthog-portability.md#mining-console-windows-and-linux)
and [benchmark/calibration guide](benchmark-calibration.md).

## November 2, 2026 acceptance report (historical)

Completed on the available hardware on 2026-11-02. All four milestones retain
their original scope; physical hardware coverage limits are listed below.
The measurements, suite counts, calibration thresholds and timing below describe
that earlier build. Later adaptive calibration changed the quick planner and
promotion policy; use the current calibration guide for present behavior.

Distribution policy was subsequently changed at the user's request: source and
release archives exclude NVIDIA CUDA/NVRTC binaries. Install the Toolkit
separately using [CUDA setup](cuda-setup.md). Earlier bundled-library smoke logs
below remain historical validation of that earlier miner executable, not a list
of files in the current archives. Final precommit builds/tests also cover whitespace-only cleanup of imported
kernel/hash sources (`precommit-formatting-audit.json`). That cleanup changes
the build identity, so earlier profiles invalidate normally. No new sustained
performance claim is made for formatting changes. That build's no-CUDA archive checks
and tests using user-installed NVRTC are recorded in `out/bench/unbundled-release-audit.json`.

## November 2 acceptance checklist (historical)

### Shared benchmark

- [x] Offline command needs no wallet or server and uses deterministic `warthog-header-sequence-v1` work.
- [x] Actual CPU/GPU workers, candidate queue and adaptive filter are shared with mining.
- [x] CPU known answers and GPU-versus-CPU checks run before timing; sampled GPU candidates are checked during timing.
- [x] Warmup is separated from measured work. Combined Janus, stage rates, interval-weighted statistics, identity, queue diagnostics and versioned JSON are exported.
- [x] Sustained Windows/Linux runs were sequential. Exported durations, means and deviations were independently recomputed from samples.

### Optional calibration

- [x] Quick staged CPU, GPU/PCIe and combined-pipeline screening finishes in about 69 seconds on this one-GPU host.
- [x] CUDA batch/block sizes and CPU thread counts are screened by measured throughput; manual dimensions remain fixed.
- [x] Defaults and plausible finalists receive repeated 300-second measurements after 30-second warmup.
- [x] Promotion requires both paired gains of at least 1% and a mean ratio of at least 1.5%, correct work, acceptable variation, stable baselines and no reported GPU thermal limit.
- [x] Without convincing improvement, stable defaults are retained. Invalid, failing or inconclusive evidence cannot promote a profile.
- [x] Cache identity includes CPU/thread information, OS, GPU UUID/architecture, driver release/API, compiler/builtins file hashes, miner build and manual constraints.
- [x] Malformed, stale, oversized and out-of-range profiles are rejected. Actual tests verified reload, build/compiler changes and manual constraints.
- [x] Explicit quick retune runs again and leaves the validated profile byte-for-byte unchanged. Normal mining reloads matching profiles without benchmarking.
- [x] Tuning is shared C++; current-build profiles and reproducible evidence are saved.

### Linux x86_64

- [x] POSIX networking, dynamic libraries, CPU identification, feature-dispatched hashing, signals and terminal sizing work in the native Linux build.
- [x] All 19 then-registered Linux tests passed, including actual CUDA output checks, portable hashing, emulated older Intel/AMD CPUs, cache policy, optional NVML, benchmark, Stratum lifecycle and PTY rendering.
- [x] Real solo-node jobs, authorization, controlled disconnect/reconnect and graceful shutdown were verified. Linux solo sessions totaled over an hour; they found no block.
- [x] The final Linux package completed five minutes on the user-authorized HeroMiners pool with 14 accepted submissions, zero rejects and clean shutdown.
- [x] Sustained controlled GPU benchmark runs and a portable Linux package/build guide are provided.

### Monitoring and release

- [x] Optional NVML reads temperature, utilization, power, clocks and clock-limit reasons where exposed.
- [x] Unsupported power readings display N/A and export null. Mining and CUDA benchmarks work with NVML disabled.
- [x] One table stays pinned above scrolling events. Resize, delayed sensor-panel growth and immediate acceptance updates pass Windows console/Linux PTY checks.
- [x] CUDA 13.4.92 is verified with direct cubin compilation on this driver. The separately available 12.9.86 compiler also executes correctly.
- [x] Forced CUDA detection failure uses OpenCL on Windows and CPU on Linux without an OpenCL runtime.
- [x] Windows Release passes 16/16 tests. Both fresh packages pass known-answer, installed-library, alternate-compiler, monitoring-absence and relocated-profile checks.
- [x] Generic launchers, instructions, upstream notices, archive checksums, logs and this acceptance checklist are saved. Host-specific profiles and the personal solo launcher are excluded from packages.

## Sustained benchmark measurements

Every full run below contains exactly 300 measured seconds after 30 seconds of
warmup. These are offline fixture results; historical BzMiner node-run results
use different work and conditions.

| Platform/configuration | Run 1 Janus | Run 2 Janus | Decision |
| --- | ---: | ---: | --- |
| Windows, defaults: 16 threads / 64M / 256 | 476.18 MH/s | 476.50 MH/s | Retained |
| Windows, 14-thread finalist / 64M / 256 | 479.80 MH/s | 479.13 MH/s | Gain below promotion threshold |
| Linux/WSL2, defaults: 16 threads / 64M / 256 | 472.80 MH/s | 470.25 MH/s | Retained |

Linux's quick 14-thread screen was slower and was excluded from full validation.
Quick screening took 68.22 seconds on Windows and 68.98 seconds on Linux.
The later explicit Windows quick retune also preserved its validated profile.

The final Windows package was separately tested with the user's 14-thread
override: five minutes, 10 accepted HeroMiners submissions, zero rejects and
clean Ctrl+C shutdown. The Linux package used its loaded 16-thread profile:
five minutes, 14 accepted submissions, zero rejects and clean shutdown.
These establish actual pool acceptance. Solo-node acceptance and a confirmed
chain reward are separate observations.

## Evidence index

Historical evidence is retained in the development workspace, not in a fresh
clone or release archive. File names below are under ignored `out/bench/` unless
otherwise stated. The November 9 checks are linked above.

| Requirement | Authoritative evidence |
| --- | --- |
| Final precommit builds/tests | `windows-precommit-build.log`, `linux-precommit-build.log`, `windows-precommit-tests.log`, `linux-precommit-tests.log` |
| Sustained-test builds/tests | `windows-build-sensor-fix.log`, `linux-build-sensor-fix.log`, `windows-tests-sensor-fix.log`, `linux-tests-sensor-fix.log` |
| Console regression and fix | `linux-console-regression-before.log`, `windows-console-sensor-fix.log`, `linux-console-sensor-fix.log` |
| Actual pinned Windows mining/profile load | `windows-live-console-current.log`, `dashboard-live-node-final.txt`, `dashboard-profile-load-final.txt` |
| Current calibration | `full-calibration-windows-release.log`, `full-calibration-linux-release.log`; profiles/results under `out/calibration-windows-release/` and `out/calibration-linux-release/` |
| Independent numeric audit | `final-runs-audit.json`, `windows-release-export-audit.json` |
| Quick retune preservation | `quick-retune-windows-release.log`, `quick-retune-preserved-profile.json` |
| Cache validation/constraints/invalidation | `profile-*-windows.log`, `profile-*-linux.log`, `profile-build-change-windows.log`, `profile-reload-windows-release.log`, `profile-malformed-current.log`, `profile-manual-current.log`; calibration unit tests |
| Real-node lifecycle | `windows-real-node-reconnect-current.log`, `linux-real-node-reconnect-current.log`, `linux-node-long.log`, `linux-node-final.log` |
| Fresh package functionality | `release-smoke-windows/release-smoke.json`, `release-smoke-linux/release-smoke.json` |
| Final pool acceptance | `windows-herominers-current.log`, `linux-herominers-current.log` |
| Automatic fallback | `fallback-auto-windows.json`, `fallback-auto-linux.json` |
| Current distribution | `unbundled-release-audit.json`; `unbundled-final-smoke-windows/`, `unbundled-final-smoke-linux/` |
| Historical payload integrity/notices | `windows-package-audit.json`, `linux-package-audit.json`, `linux-nvrtc-*-verified.log`, `cuda*-runtime-dependencies.log` |
| Sequencing and terminal outcomes | `finish-release-runs.log`, `release-runs-state.json` |

Sustained-test build identities (before formatting-only source cleanup):

- Windows: `12f21fb246c0d53b5cd353eaceaf135db433a0e981608e85a14f29e11dd68fd5`
- Linux: `8ea47f4d1d12de9e3c4d5dbf66e90a0a80932ae0ca3b52bcea1ac13b04c89e52`

Profiles, archives and results retain their original build identities.
They are separated from validation of subsequent packaging and formatting work.

## Artifacts and commands

Archives and SHA-256 sidecars are under `out/release/`:

- `BRminer-1.1.0-windows-x64.zip`
- `BRminer-1.1.0-linux-x64.tar.gz`

Extract the release archive. Install optional NVRTC separately. The ZIP/tar includes generic
mining, benchmark and calibration launchers. The private solo launcher remains
in the source workspace with its requested 14 threads and is ignored by Git.

See [benchmark and calibration commands](benchmark-calibration.md),
[Linux setup](warthog-portability.md), and
[the source architecture map](knowledge-graph.md).

## Validation coverage

- Physical host: AMD Ryzen 7 5800X, 16 logical threads, RTX 4060, NVIDIA 610.88.
- Windows x64 and native Ubuntu 24.04 x86_64 binaries were exercised; Linux ran
  under Docker Desktop/WSL2 on the same GPU. Its timings include that environment.
- CUDA driver API reports 13.3; tested NVRTC 13.4.92 emits direct cubin output.
  The matching 13.4 and 12.9 pairs were validated; they are installed separately
  by users and are excluded from source and release archives.
- Intel Nehalem and AMD Opteron feature sets without AVX were emulated for hash
  correctness. Physical Intel performance was not measured.
- Linux OpenCL execution was unavailable; its automatic CPU fallback passed.
  Windows OpenCL fallback executed actual GPU work.
- Bare-metal Linux, additional physical AMD/Intel systems, other NVIDIA
  architectures and multi-GPU configurations remain untested.
- The second Windows PC has no remote access. The included five-minute benchmark
  launcher produces JSON for manual testing there; current-version results from
  that PC have not been claimed.
