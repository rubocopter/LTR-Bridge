# XeSS Native AA D3D12 probe

Status: **implemented and host-tested on one NVIDIA RTX 4070 Ti / driver 32.0.16.1692; coherent harness jitter/MV semantics and first GPU-time/temporary-memory measurements implemented; visual quality and broader performance validation pending**.

This optional Phase 1 probe is the first real reconstruction backend executed by LTR Bridge. It is intentionally separate from the D3D11 temporal harness and does not make XeSS or D3D12 mandatory architecture.

## Exact upstream dependency

- Intel XeSS SDK: `v3.0.2`
- Commit: `8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0`
- Repository: `https://github.com/intel/xess`

The SDK is not vendored. Configure the probe against an external checkout with:

```powershell
cmake -S . -B build-xess -A x64 `
  -DLTR_ENABLE_XESS_NATIVE_AA_PROBE=ON `
  -DLTR_XESS_SDK_ROOT=C:\path\to\xess
cmake --build build-xess --config Release
ctest --test-dir build-xess -C Release --output-on-failure
```

For repeated measurements across resolutions, use `tools/measure_xess_native_aa.ps1`. It configures an isolated ignored build directory per resolution, runs the probe repeatedly, checks the reported GPU timing and temporary-heap lines, and writes a CSV summary under `build-root/` by default. These measurements remain experiment tooling; they do not change the probe's default `256x144`, 12-frame configuration.

CMake copies only `libxess.dll` beside the local probe executable for that build. It is not added to source control or redistribution artifacts.

## Probe contract

The probe creates D3D12 resources directly and uploads deterministic synthetic input; it does not copy Intel sample renderer code.

- output and input: `256x144`;
- quality: `XESS_QUALITY_SETTING_AA` (`1.0x` Native AA);
- color: `R8G8B8A8_UNORM`, declared LDR;
- jitter: the same shared 8-sample Halton sequence used by the D3D11 harness;
- motion: high-resolution `R16G16_FLOAT`, current-to-previous in render pixels, jitter excluded and dilated for the moving rigid-object proxy;
- history reset: explicit on frame 0;
- optional responsive-pixel mask: `R8_UNORM`, set only where the synthetic screen-space HUD membership changes;
- output: `R8G8B8A8_UNORM` UAV with deterministic GPU readback.

This deliberately answers a narrow question: can the provisional content-classification requirement discovered by the HUD harness be mapped to a real backend concept and exercised through a real reconstruction runtime?

## Host result — 2026-09-14

The local host test used `NVIDIA GeForce RTX 4070 Ti` and executed 12 frames through two independent XeSS Native AA contexts: one baseline and one with `XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK`. The synthetic scene now follows the harness temporal convention rather than the earlier zero-jitter/zero-MV bootstrap: both paths consume the same Halton jitter sequence, while a translating rigid-object proxy supplies current-to-previous pixel motion with jitter excluded. The final frame contained `1,472` visible moving-object pixels and `1,632` pixels in the one-pixel-dilated MV region.

The final changed-HUD region measured:

- baseline mean absolute RGB error to current input: `8.3259`;
- responsive-mask mean absolute RGB error: `5.6902`;
- output hashes differed, proving the mask path affected reconstruction output;
- probe result: `PASS`.

The same run also records D3D12 timestamp intervals around `xessD3D12Execute` after two warm-up frames:

- baseline: mean `0.1886 ms`, min `0.1874 ms`, max `0.1894 ms`;
- responsive-mask context: mean `0.1895 ms`, min `0.1884 ms`, max `0.1905 ms`.

`xessGetProperties` reported `65,536` bytes of temporary buffer heap plus `1,835,008` bytes of temporary texture heap for each context, or `1,900,544` bytes (`1.8125 MiB`) of declared temporary heap capacity per context at `256x144` Native AA. These are SDK-reported temporary heap requirements, not total process/GPU VRAM consumption.

The root harness and optical-flow regression also passed in the same build: `3/3` CTest tests.

**Observed:** XeSS's responsive-pixel mask is a concrete backend mapping for independently changing content that geometry depth/MV/history identity cannot describe. In this synthetic moving-HUD case it moved the reconstruction materially closer to the current-frame input.

This does not establish a universal HUD policy. The mask semantics, generation source, coverage, transparency behavior and real-game content still require separate validation. The GPU timing and temporary-memory numbers are a single low-resolution host measurement and therefore do not constitute `performance-validated` evidence.

## Evidence boundary

**Implemented:** optional build integration and deterministic D3D12 input/output path.

**Host-tested:** XeSS 3.0.2 Native AA initialization, 1.0x input-resolution query, shared harness Halton jitter, current-to-previous high-resolution dilated MV, explicit reset, responsive-mask execution, D3D12 timestamp readback, `xessGetProperties` temporary-heap reporting and GPU output readback on the local RTX 4070 Ti.

**Observed:** the responsive mask reduced final changed-region MAE from `8.3259` to `5.6902` for the coherent-jitter/MV HUD sequence. On this host/run the XeSS execute interval was approximately `0.189 ms` at `256x144`, and each context requested `1.8125 MiB` of temporary heap capacity.

**Experiment-pending:** direct renderer-resource transfer rather than semantic reproduction, blended-transparency mapping, repeated/resolution-scaled performance and memory measurements, visual inspection, SR ratios, other GPUs/drivers, and stereo/per-eye execution.
