// SPDX-License-Identifier: MIT
#include <dlfcn.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

static constexpr int SRC_W = 1920;
static constexpr int SRC_H = 1080;
static constexpr int DST_W = 1280;
static constexpr int DST_H = 720;
static constexpr int SRC_STRIDE = SRC_W * 4;
static constexpr int Y_STRIDE = DST_W;
static constexpr int C_STRIDE = DST_W / 2;
static constexpr size_t SRC_BYTES = static_cast<size_t>(SRC_STRIDE) * SRC_H;
static constexpr size_t Y_BYTES = static_cast<size_t>(Y_STRIDE) * DST_H;
static constexpr size_t C_BYTES = static_cast<size_t>(C_STRIDE) * (DST_H / 2);
static constexpr size_t DST_BYTES = Y_BYTES + 2 * C_BYTES;

struct Api {
    void *swscale = nullptr;
    void *avutil = nullptr;

    using sws_alloc_context_fn = SwsContext *(*)();
    using sws_init_context_fn = int (*)(SwsContext *, SwsFilter *, SwsFilter *);
    using sws_freeContext_fn = void (*)(SwsContext *);
    using sws_scale_fn = int (*)(SwsContext *, const uint8_t *const [], const int [], int, int, uint8_t *const [], const int []);
    using av_opt_set_int_fn = int (*)(void *, const char *, int64_t, int);

    sws_alloc_context_fn sws_alloc_context_p = nullptr;
    sws_init_context_fn sws_init_context_p = nullptr;
    sws_freeContext_fn sws_freeContext_p = nullptr;
    sws_scale_fn sws_scale_p = nullptr;
    av_opt_set_int_fn av_opt_set_int_p = nullptr;

    ~Api() {
        if (swscale) dlclose(swscale);
        if (avutil) dlclose(avutil);
    }
};

static void *open_any(const std::vector<const char *> &names)
{
    for (const char *name : names) {
        if (void *h = dlopen(name, RTLD_NOW | RTLD_LOCAL)) {
            std::cout << "Loaded " << name << "\n";
            return h;
        }
    }
    return nullptr;
}

static void *sym(void *h, const char *name)
{
    dlerror();
    void *p = dlsym(h, name);
    if (const char *e = dlerror()) {
        std::cerr << "dlsym(" << name << ") failed: " << e << "\n";
        return nullptr;
    }
    return p;
}

static bool load_api(Api &api)
{
    api.avutil = open_any({"libavutil.so", "libavutil.so.60", "libavutil.so.59", "libavutil.so.58", "libavutil.so.57", "libavutil.so.56"});
    api.swscale = open_any({"libswscale.so", "libswscale.so.9", "libswscale.so.8", "libswscale.so.7", "libswscale.so.6", "libswscale.so.5"});
    if (!api.avutil || !api.swscale) return false;

    api.sws_alloc_context_p = reinterpret_cast<Api::sws_alloc_context_fn>(sym(api.swscale, "sws_alloc_context"));
    api.sws_init_context_p = reinterpret_cast<Api::sws_init_context_fn>(sym(api.swscale, "sws_init_context"));
    api.sws_freeContext_p = reinterpret_cast<Api::sws_freeContext_fn>(sym(api.swscale, "sws_freeContext"));
    api.sws_scale_p = reinterpret_cast<Api::sws_scale_fn>(sym(api.swscale, "sws_scale"));
    api.av_opt_set_int_p = reinterpret_cast<Api::av_opt_set_int_fn>(sym(api.avutil, "av_opt_set_int"));

    return api.sws_alloc_context_p && api.sws_init_context_p && api.sws_freeContext_p && api.sws_scale_p && api.av_opt_set_int_p;
}

static uint64_t checksum(const uint8_t *p, size_t n)
{
    uint64_t s = 0;
    for (size_t i = 0; i < n; i += 4096) s = (s * 1315423911ULL) ^ p[i];
    if (n) s ^= p[n - 1];
    return s;
}

struct Context {
    Api &api;
    SwsContext *ctx = nullptr;

    explicit Context(Api &a) : api(a) {}
    ~Context() { if (ctx) api.sws_freeContext_p(ctx); }
};

static bool set_i(Api &api, SwsContext *ctx, const char *name, int64_t value)
{
    int r = api.av_opt_set_int_p(ctx, name, value, 0);
    if (r < 0) {
        std::cerr << "av_opt_set_int(" << name << "=" << value << ") failed: " << r << "\n";
        return false;
    }
    return true;
}

static bool run_case(Api &api, int threads, int runs, int warmup,
                     const uint8_t *src, uint8_t *dst)
{
    Context c(api);
    c.ctx = api.sws_alloc_context_p();
    if (!c.ctx) {
        std::cerr << "sws_alloc_context failed\n";
        return false;
    }

    // Match Sunshine's avcodec_software_encode_device_t::init() for this exact case.
    if (!set_i(api, c.ctx, "srcw", SRC_W) ||
        !set_i(api, c.ctx, "srch", SRC_H) ||
        !set_i(api, c.ctx, "src_format", AV_PIX_FMT_BGR0) ||
        !set_i(api, c.ctx, "dstw", DST_W) ||
        !set_i(api, c.ctx, "dsth", DST_H) ||
        !set_i(api, c.ctx, "dst_format", AV_PIX_FMT_YUV420P) ||
        !set_i(api, c.ctx, "sws_flags", SWS_LANCZOS | SWS_ACCURATE_RND) ||
        !set_i(api, c.ctx, "threads", threads)) {
        return false;
    }

    if (api.sws_init_context_p(c.ctx, nullptr, nullptr) < 0) {
        std::cerr << "sws_init_context failed for threads=" << threads << "\n";
        return false;
    }

    const uint8_t *src_data[4] = {src, nullptr, nullptr, nullptr};
    int src_linesize[4] = {SRC_STRIDE, 0, 0, 0};

    uint8_t *dst_data[4] = {
        dst,
        dst + Y_BYTES,
        dst + Y_BYTES + C_BYTES,
        nullptr
    };
    int dst_linesize[4] = {Y_STRIDE, C_STRIDE, C_STRIDE, 0};

    auto one = [&]() -> bool {
        int out = api.sws_scale_p(c.ctx, src_data, src_linesize, 0, SRC_H, dst_data, dst_linesize);
        return out == DST_H;
    };

    for (int i = 0; i < warmup; ++i) {
        if (!one()) {
            std::cerr << "warmup sws_scale failed\n";
            return false;
        }
    }

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < runs; ++i) {
        if (!one()) {
            std::cerr << "timed sws_scale failed\n";
            return false;
        }
    }
    auto t1 = std::chrono::steady_clock::now();

    double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double avg_ms = total_ms / runs;
    double fps = 1000.0 / avg_ms;
    double gib_s = (static_cast<double>(SRC_BYTES + DST_BYTES) / (1024.0 * 1024.0 * 1024.0)) / (avg_ms / 1000.0);

    std::cout << std::fixed << std::setprecision(5)
              << "swscale threads=" << threads
              << ": runs=" << runs
              << " warmup=" << warmup
              << " total_ms=" << total_ms
              << " avg_ms=" << avg_ms
              << " fps=" << fps
              << " effective_GiB/s=" << gib_s
              << " checksum=" << checksum(dst, DST_BYTES)
              << "\n";
    return true;
}

int main(int argc, char **argv)
{
    int runs = 300;
    int warmup = 20;
    if (argc > 1) runs = std::max(1, std::atoi(argv[1]));
    if (argc > 2) warmup = std::max(0, std::atoi(argv[2]));

    std::cout << "Sunshine-matched libswscale benchmark\n"
              << "BGR0 1920x1080 -> YUV420P 1280x720\n"
              << "flags=SWS_LANCZOS|SWS_ACCURATE_RND\n"
              << "Sunshine default min_threads=2\n"
              << "compile-time AV_PIX_FMT_BGR0=" << static_cast<int>(AV_PIX_FMT_BGR0)
              << " AV_PIX_FMT_YUV420P=" << static_cast<int>(AV_PIX_FMT_YUV420P)
              << "\n";

    Api api;
    if (!load_api(api)) {
        std::cerr << "FAIL: could not load required Batocera FFmpeg libraries/symbols\n";
        return 2;
    }

    std::vector<uint8_t> src(SRC_BYTES);
    std::vector<uint8_t> dst(DST_BYTES);

    for (size_t p = 0; p < static_cast<size_t>(SRC_W) * SRC_H; ++p) {
        src[p * 4 + 0] = static_cast<uint8_t>((p * 3 + 11) & 0xff); // B
        src[p * 4 + 1] = static_cast<uint8_t>((p * 5 + 29) & 0xff); // G
        src[p * 4 + 2] = static_cast<uint8_t>((p * 7 + 47) & 0xff); // R
        src[p * 4 + 3] = 0;
    }

    // Sunshine default first, then controls.
    if (!run_case(api, 2, runs, warmup, src.data(), dst.data())) return 3;
    if (!run_case(api, 1, runs, warmup, src.data(), dst.data())) return 3;
    if (!run_case(api, 4, runs, warmup, src.data(), dst.data())) return 3;

    std::cout << "NOTE: measures libswscale conversion+Lanczos scaling only; excludes capture, libx264, scheduling and synchronization.\n";
    return 0;
}
