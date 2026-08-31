// SPDX-License-Identifier: MIT
#include <arm_neon.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static constexpr int W = 1920;
static constexpr int H = 1080;
static constexpr size_t PIXELS = static_cast<size_t>(W) * H;
static constexpr size_t SRC_BYTES = PIXELS * 4;
static constexpr size_t DST_BYTES = PIXELS * 3;

static inline void bgr0_to_rgb888_scalar(const uint8_t *src, uint8_t *dst)
{
    for (size_t i = 0, j = 0; i < SRC_BYTES; i += 4, j += 3) {
        dst[j + 0] = src[i + 2];
        dst[j + 1] = src[i + 1];
        dst[j + 2] = src[i + 0];
    }
}

static inline void bgr0_to_rgb888_neon(const uint8_t *src, uint8_t *dst)
{
    size_t p = 0;
    for (; p + 16 <= PIXELS; p += 16) {
        const uint8x16x4_t bgra = vld4q_u8(src + p * 4);
        uint8x16x3_t rgb;
        rgb.val[0] = bgra.val[2];
        rgb.val[1] = bgra.val[1];
        rgb.val[2] = bgra.val[0];
        vst3q_u8(dst + p * 3, rgb);
    }
    for (; p < PIXELS; ++p) {
        const uint8_t *s = src + p * 4;
        uint8_t *d = dst + p * 3;
        d[0] = s[2]; d[1] = s[1]; d[2] = s[0];
    }
}

static uint64_t checksum(const uint8_t *p, size_t n)
{
    uint64_t s = 0;
    for (size_t i = 0; i < n; i += 4096) s = (s * 1315423911ULL) ^ p[i];
    if (n) s ^= p[n - 1];
    return s;
}

template <typename F>
static void bench(const std::string &name, F fn, const uint8_t *src, uint8_t *dst,
                  int warmup, int runs, size_t bytes_touched)
{
    for (int i = 0; i < warmup; ++i) fn(src, dst);

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < runs; ++i) fn(src, dst);
    auto t1 = std::chrono::steady_clock::now();

    const double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double avg_ms = total_ms / runs;
    const double fps = 1000.0 / avg_ms;
    const double gib_s = (static_cast<double>(bytes_touched) / (1024.0 * 1024.0 * 1024.0)) / (avg_ms / 1000.0);

    std::cout << name
              << ": runs=" << runs
              << " warmup=" << warmup
              << " total_ms=" << total_ms
              << " avg_ms=" << avg_ms
              << " fps=" << fps
              << " GiB/s=" << gib_s
              << " checksum=" << checksum(dst, DST_BYTES)
              << "\n";
}

int main(int argc, char **argv)
{
    int runs = 300;
    int warmup = 20;
    if (argc > 1) runs = std::max(1, std::stoi(argv[1]));
    if (argc > 2) warmup = std::max(0, std::stoi(argv[2]));

    std::vector<uint8_t> src(SRC_BYTES);
    std::vector<uint8_t> dst(DST_BYTES);
    std::vector<uint8_t> ref(DST_BYTES);

    for (size_t p = 0; p < PIXELS; ++p) {
        src[p * 4 + 0] = static_cast<uint8_t>((p * 3 + 11) & 0xff); // B
        src[p * 4 + 1] = static_cast<uint8_t>((p * 5 + 29) & 0xff); // G
        src[p * 4 + 2] = static_cast<uint8_t>((p * 7 + 47) & 0xff); // R
        src[p * 4 + 3] = 0;                                        // X
    }

    bgr0_to_rgb888_scalar(src.data(), ref.data());
    bgr0_to_rgb888_neon(src.data(), dst.data());
    if (std::memcmp(ref.data(), dst.data(), DST_BYTES) != 0) {
        std::cerr << "FAIL: NEON output differs from scalar reference\n";
        return 1;
    }

    std::cout << "BGR0->RGB888 1920x1080 CPU benchmark\n";
    std::cout << "src_bytes=" << SRC_BYTES << " dst_bytes=" << DST_BYTES
              << " bytes_per_frame_read_write=" << (SRC_BYTES + DST_BYTES) << "\n";

    bench("scalar", bgr0_to_rgb888_scalar, src.data(), dst.data(), warmup, runs, SRC_BYTES + DST_BYTES);
    bench("neon", bgr0_to_rgb888_neon, src.data(), dst.data(), warmup, runs, SRC_BYTES + DST_BYTES);

    auto memcpy_fn = [](const uint8_t *s, uint8_t *d) { std::memcpy(d, s, DST_BYTES); };
    bench("memcpy_6.22MiB", memcpy_fn, src.data(), dst.data(), warmup, runs, DST_BYTES * 2);

    std::cout << "NOTE: scalar/NEON read 8.29 MiB BGR0 and write 6.22 MiB RGB888 per frame.\n";
    std::cout << "NOTE: benchmark excludes capture, PiSP, encoder, scheduling and synchronization.\n";
    return 0;
}
