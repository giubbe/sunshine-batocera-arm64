# Project status: Raspberry Pi 5 / Batocera ARM64 Sunshine build

This repository now represents the last configuration that was validated on the real Raspberry Pi 5 target with correct graphics and the experimental PiSP path enabled.

The maintainer is pausing active development here. Contributions are welcome, especially from people interested in Raspberry Pi 5 graphics, KMS/DRM capture, PipeWire scheduling, libpisp, FFmpeg/libx264 or Sunshine internals.

## Validated configuration

Target used for runtime validation:

- Raspberry Pi 5 / BCM2712 / Cortex-A76
- Batocera `43apu.1`
- AArch64
- Sunshine `v2026.516.143833`
- libpisp 1.7.0
- KMS capture
- software H.264 encoding with libx264

The PiSP path is:

```text
KMS capture
  -> BGRA/BGR0 RAM readback
  -> NEON BGR0 -> RGB888 repack
  -> PiSP scaling + RGB/YUV conversion
  -> YUV420P
  -> libx264
```

The KMS `GL_BGRA` readback is intentionally kept unchanged because it is the last path observed to be visually correct and stable on the tested target.

PiSP runtime validation includes both normal scaling and padded output. A 640x480 Flycast source was successfully converted to 960x720 content centered in a 1280x720 output, with stride-aware padding. A controlled end-to-end benchmark measured Sunshine process CPU at 66.19% without PiSP versus 52.29% with PiSP in the tested scenario, approximately a 21% relative reduction. This is not claimed as a universal gain.

## Audio thread-priority fix

Sunshine requests high/critical priority for its audio threads. On the tested Batocera system RTKit is unavailable and the fallback `setpriority()` calls originally failed with `EPERM` because `CAP_SYS_NICE` was present in the permitted capability set but not effective.

The current build temporarily enables effective `CAP_SYS_NICE` only around the existing `setpriority()` fallback, then restores the previous effective capability state.

Runtime validation on the real Pi 5 confirmed:

- capture/session audio thread reaches nice `-15`;
- `audio::encode` reaches nice `-10`;
- the capability is cleared from the effective set again after the call;
- the previous `setpriority failed ... Permission denied` condition is fixed.

This is a correctness fix. It did not eliminate the remaining audio underruns described below.

## Current limit

The remaining practical limit is audio stability under demanding streaming workloads.

Controlled recordings from `sink-sunshine-stereo.monitor` show structured exact-zero stereo gaps while streaming. The gaps are aligned to 128-frame units at 48 kHz, strongly suggesting periodic buffer starvation or missed graph deadlines. Because the gaps are already present in the local PipeWire monitor recording, these particular gaps originate upstream of Sunshine's Opus encoding, network transport and Moonlight playback.

The effect is strongly workload-dependent. In the measured Daytona tests, the amount of exact-zero audio increased with the non-Vulkan rendering load:

- 480-class test: about 4.06% zero duration;
- 720-class test: about 10.96%;
- earlier 1080-class test: about 20.79%.

A 720 Vulkan test reduced the zero duration to about 0.70%, but Vulkan was not accepted as a solution because the emulator's visual output was substantially worse on the tested setup. This result is still useful evidence: it suggests that renderer/resource pressure materially affects the audio deadline margin.

The exact bottleneck is not proven. Possible areas include CPU scheduling, GPU/driver stalls, memory-bandwidth contention, PipeWire graph timing and software H.264 encoding pressure.

## Experiment intentionally not merged: RGB888 KMS readback

An additional experiment attempted to remove the CPU NEON repack by changing Sunshine's KMS RAM readback from upstream `GL_BGRA` / 4 bytes per pixel to `GL_RGB` / 3 bytes per pixel and feeding RGB888 directly into PiSP.

The experiment compiled and passed the native ARM64 CI. Runtime markers confirmed that the direct RGB888 path was actually used on the Raspberry Pi 5 and the NEON BGR0 -> RGB888 repack was bypassed.

However, the image was not reliable. Intermittent black lines, tearing-like corruption and block-wise transitions were observed. The problem was visible not only in Flycast but also in Batocera's own GUI fades, where upper and lower parts of the screen could update at different apparent moments. The corruption was also observed when the RGB888 capture path subsequently fell back to libswscale, so the experiment points toward the altered `GL_RGB` readback/staging path rather than PiSP itself.

For that reason this experiment is not promoted to `main` and should not be treated as a working optimization.

## PiSP hardware constraint discovered

The tested Pi reports PiSP backend hardware revision `0x2252700`, corresponding to the BCM2712 C0 backend used by the pinned libpisp version. That backend does not expose the RGB32 support required for a straightforward direct XRGB8888/RGBX8888 PiSP path.

Therefore a future optimization should not assume that the existing KMS XRGB8888 framebuffer can simply be handed directly to PiSP on this hardware revision.

## Useful directions for contributors

Contributions are especially welcome around these areas:

1. Preserve the visually stable upstream KMS `GL_BGRA` capture path while reducing the cost of the BGR0 -> RGB888 preparation step.
2. Investigate whether a safe DMA-BUF or alternate PiSP input path exists for BCM2712 C0 without relying on unsupported RGB32 backend features.
3. Profile PipeWire graph timing (`QUANT`, `WAIT`, `BUSY`, `ERR/xruns`) under the same controlled workloads and correlate it with CPU/GPU pressure.
4. Investigate scheduler, memory-bandwidth and V3D/Mesa interactions that could explain why Vulkan leaves substantially more audio headroom than the visually preferred renderer.
5. Explore further libx264-side reductions that do not compromise stream quality or compatibility.
6. Reproduce the benchmark and audio measurements on other Pi 5 / Batocera versions before generalizing any result.

Please keep new experiments isolated, measurable and reversible. A useful contribution should state the exact target, source revisions, build flags, runtime configuration and before/after evidence.

## What is considered working today

The intended public baseline is the build with:

- the validated PiSP converter and libswscale fallback;
- upstream KMS `GL_BGRA` capture semantics;
- upstream PulseAudio `pa_simple` capture path;
- the validated temporary `CAP_SYS_NICE` fallback for requested audio-thread nice levels;
- native ARM64 CI build/audit gates;
- no RGB888 KMS-readback experiment.

This baseline has correct graphics on the tested target and is the recommended starting point for further community work.
