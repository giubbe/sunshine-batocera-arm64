// SPDX-License-Identifier: MIT
#include <dlfcn.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
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
    using alloc_fn = SwsContext *(*)();
    using init_fn = int (*)(SwsContext *, SwsFilter *, SwsFilter *);
    using free_fn = void (*)(SwsContext *);
    using scale_fn = int (*)(SwsContext *, const uint8_t *const [], const int [], int, int, uint8_t *const [], const int []);
    using opt_int_fn = int (*)(void *, const char *, int64_t, int);
    alloc_fn alloc = nullptr;
    init_fn init = nullptr;
    free_fn free_ctx = nullptr;
    scale_fn scale = nullptr;
    opt_int_fn opt_int = nullptr;
    ~Api() { if (swscale) dlclose(swscale); if (avutil) dlclose(avutil); }
};

static void *open_any(const std::vector<const char *> &names) {
    for (auto *n : names) if (void *h = dlopen(n, RTLD_NOW | RTLD_LOCAL)) { std::cout << "Loaded " << n << "\n"; return h; }
    return nullptr;
}

static bool load(Api &a) {
    a.avutil = open_any({"libavutil.so","libavutil.so.60","libavutil.so.59","libavutil.so.58","libavutil.so.57"});
    a.swscale = open_any({"libswscale.so","libswscale.so.9","libswscale.so.8","libswscale.so.7","libswscale.so.6"});
    if (!a.avutil || !a.swscale) return false;
    a.alloc = reinterpret_cast<Api::alloc_fn>(dlsym(a.swscale,"sws_alloc_context"));
    a.init = reinterpret_cast<Api::init_fn>(dlsym(a.swscale,"sws_init_context"));
    a.free_ctx = reinterpret_cast<Api::free_fn>(dlsym(a.swscale,"sws_freeContext"));
    a.scale = reinterpret_cast<Api::scale_fn>(dlsym(a.swscale,"sws_scale"));
    a.opt_int = reinterpret_cast<Api::opt_int_fn>(dlsym(a.avutil,"av_opt_set_int"));
    return a.alloc && a.init && a.free_ctx && a.scale && a.opt_int;
}

static double read_num(const char *path, double scale = 1.0) {
    std::ifstream f(path);
    double v = NAN;
    if (f >> v) return v * scale;
    return NAN;
}

static double cpu_mhz() {
    double sum = 0; int n = 0;
    for (int i = 0; i < 4; ++i) {
        std::string p = "/sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/scaling_cur_freq";
        double khz = read_num(p.c_str());
        if (!std::isnan(khz)) { sum += khz / 1000.0; ++n; }
    }
    return n ? sum / n : NAN;
}

static double temp_c() {
    double t = read_num("/sys/class/thermal/thermal_zone0/temp");
    return std::isnan(t) ? NAN : t / 1000.0;
}

static double percentile(std::vector<double> v, double p) {
    if (v.empty()) return NAN;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(std::ceil(p * v.size())) - 1;
    if (idx >= v.size()) idx = v.size() - 1;
    return v[idx];
}

static uint64_t checksum(const uint8_t *p, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i += 4096) s = (s * 1315423911ULL) ^ p[i];
    if (n) s ^= p[n - 1];
    return s;
}

int main(int argc, char **argv) {
    int seconds = argc > 1 ? std::max(1, std::atoi(argv[1])) : 60;
    int threads = argc > 2 ? std::max(1, std::atoi(argv[2])) : 2;

    std::cout << "Sunshine sustained libswscale benchmark\n"
              << "BGR0 1920x1080 -> YUV420P 1280x720\n"
              << "flags=SWS_LANCZOS|SWS_ACCURATE_RND threads=" << threads << " duration=" << seconds << "s\n";

    Api a;
    if (!load(a)) { std::cerr << "FAIL: FFmpeg runtime symbols unavailable\n"; return 2; }

    SwsContext *ctx = a.alloc();
    if (!ctx) return 3;
    auto cleanup = [&] { a.free_ctx(ctx); };

    auto set = [&](const char *n, int64_t v) { return a.opt_int(ctx, n, v, 0) >= 0; };
    if (!set("srcw",SRC_W) || !set("srch",SRC_H) || !set("src_format",AV_PIX_FMT_BGR0) ||
        !set("dstw",DST_W) || !set("dsth",DST_H) || !set("dst_format",AV_PIX_FMT_YUV420P) ||
        !set("sws_flags",SWS_LANCZOS | SWS_ACCURATE_RND) || !set("threads",threads) ||
        a.init(ctx,nullptr,nullptr) < 0) {
        cleanup(); std::cerr << "FAIL: swscale setup\n"; return 4;
    }

    std::vector<uint8_t> src(SRC_BYTES), dst(DST_BYTES);
    for (size_t p=0; p<static_cast<size_t>(SRC_W)*SRC_H; ++p) {
        src[p*4+0]=static_cast<uint8_t>((p*3+11)&255);
        src[p*4+1]=static_cast<uint8_t>((p*5+29)&255);
        src[p*4+2]=static_cast<uint8_t>((p*7+47)&255);
        src[p*4+3]=0;
    }

    const uint8_t *sd[4]={src.data(),nullptr,nullptr,nullptr};
    int ss[4]={SRC_STRIDE,0,0,0};
    uint8_t *dd[4]={dst.data(),dst.data()+Y_BYTES,dst.data()+Y_BYTES+C_BYTES,nullptr};
    int ds[4]={Y_STRIDE,C_STRIDE,C_STRIDE,0};

    for (int i=0;i<30;++i) if (a.scale(ctx,sd,ss,0,SRC_H,dd,ds)!=DST_H) { cleanup(); return 5; }

    using clock=std::chrono::steady_clock;
    auto global_start=clock::now();
    auto window_start=global_start;
    int window=0;
    uint64_t total_frames=0;
    std::vector<double> win_ms, all_ms;

    std::cout << "sec frames avg_ms p95_ms max_ms fps temp_C cpu_MHz\n";

    while (std::chrono::duration<double>(clock::now()-global_start).count() < seconds) {
        auto t0=clock::now();
        int out=a.scale(ctx,sd,ss,0,SRC_H,dd,ds);
        auto t1=clock::now();
        if (out!=DST_H) { cleanup(); std::cerr << "FAIL: sws_scale\n"; return 6; }
        double ms=std::chrono::duration<double,std::milli>(t1-t0).count();
        win_ms.push_back(ms); all_ms.push_back(ms); ++total_frames;

        if (std::chrono::duration<double>(t1-window_start).count() >= 1.0) {
            ++window;
            double avg=std::accumulate(win_ms.begin(),win_ms.end(),0.0)/win_ms.size();
            double p95=percentile(win_ms,0.95);
            double mx=*std::max_element(win_ms.begin(),win_ms.end());
            double elapsed=std::chrono::duration<double>(t1-window_start).count();
            double fps=win_ms.size()/elapsed;
            std::cout << std::fixed << std::setprecision(3)
                      << window << ' ' << win_ms.size() << ' ' << avg << ' ' << p95 << ' ' << mx << ' ' << fps << ' ';
            double tc=temp_c(), mhz=cpu_mhz();
            if (std::isnan(tc)) std::cout << "NA "; else std::cout << tc << ' ';
            if (std::isnan(mhz)) std::cout << "NA\n"; else std::cout << mhz << "\n";
            win_ms.clear(); window_start=t1;
        }
    }

    double total_s=std::chrono::duration<double>(clock::now()-global_start).count();
    double avg=std::accumulate(all_ms.begin(),all_ms.end(),0.0)/all_ms.size();
    std::cout << "SUMMARY frames=" << total_frames
              << " elapsed_s=" << std::fixed << std::setprecision(3) << total_s
              << " avg_ms=" << avg
              << " p95_ms=" << percentile(all_ms,0.95)
              << " p99_ms=" << percentile(all_ms,0.99)
              << " max_ms=" << *std::max_element(all_ms.begin(),all_ms.end())
              << " fps=" << total_frames/total_s
              << " checksum=" << checksum(dst.data(),DST_BYTES) << "\n";
    std::cout << "NOTE: conversion+scaling only; no capture, x264, queueing or network.\n";
    cleanup();
    return 0;
}
