// SPDX-License-Identifier: MIT
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <drm_fourcc.h>

#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

static std::string fourcc(uint32_t f)
{
    char s[5] = {
        static_cast<char>(f & 0xff),
        static_cast<char>((f >> 8) & 0xff),
        static_cast<char>((f >> 16) & 0xff),
        static_cast<char>((f >> 24) & 0xff),
        0
    };
    return std::string(s, 4);
}

int main(int argc, char **argv)
{
    const char *render_node = argc > 1 ? argv[1] : "/dev/dri/renderD128";

    int fd = open(render_node, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open render node");
        return 1;
    }

    gbm_device *gbm = gbm_create_device(fd);
    if (!gbm) {
        std::cerr << "gbm_create_device failed\n";
        return 1;
    }

    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!get_platform_display) {
        std::cerr << "eglGetPlatformDisplayEXT unavailable\n";
        return 1;
    }

    EGLDisplay dpy = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, nullptr);
    if (dpy == EGL_NO_DISPLAY) {
        std::cerr << "EGL GBM display failed\n";
        return 1;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        std::cerr << "eglInitialize failed: 0x" << std::hex << eglGetError() << std::dec << "\n";
        return 1;
    }

    std::cout << "EGL " << major << '.' << minor
              << " vendor=" << (eglQueryString(dpy, EGL_VENDOR) ?: "?") << "\n";

    const char *ext = eglQueryString(dpy, EGL_EXTENSIONS);
    std::cout << "EGL_EXT_image_dma_buf_import="
              << (ext && std::string(ext).find("EGL_EXT_image_dma_buf_import") != std::string::npos ? "yes" : "no")
              << "\n";
    std::cout << "EGL_EXT_image_dma_buf_import_modifiers="
              << (ext && std::string(ext).find("EGL_EXT_image_dma_buf_import_modifiers") != std::string::npos ? "yes" : "no")
              << "\n";

    auto query_formats = reinterpret_cast<PFNEGLQUERYDMABUFFORMATSEXTPROC>(
        eglGetProcAddress("eglQueryDmaBufFormatsEXT"));
    auto query_modifiers = reinterpret_cast<PFNEGLQUERYDMABUFMODIFIERSEXTPROC>(
        eglGetProcAddress("eglQueryDmaBufModifiersEXT"));

    if (!query_formats) {
        std::cerr << "eglQueryDmaBufFormatsEXT unavailable\n";
        return 2;
    }

    EGLint count = 0;
    if (!query_formats(dpy, 0, nullptr, &count)) {
        std::cerr << "eglQueryDmaBufFormatsEXT(count) failed: 0x"
                  << std::hex << eglGetError() << std::dec << "\n";
        return 2;
    }

    std::vector<EGLint> formats(count);
    EGLint got = 0;
    if (!query_formats(dpy, count, formats.data(), &got)) {
        std::cerr << "eglQueryDmaBufFormatsEXT(list) failed: 0x"
                  << std::hex << eglGetError() << std::dec << "\n";
        return 2;
    }

    std::cout << "DMA-BUF formats advertised: " << got << "\n";

    const uint32_t targets[] = {
        DRM_FORMAT_RGB888,
        DRM_FORMAT_BGR888,
        DRM_FORMAT_XRGB8888,
        DRM_FORMAT_XBGR8888,
        DRM_FORMAT_ARGB8888,
        DRM_FORMAT_ABGR8888,
    };

    for (uint32_t target : targets) {
        bool present = false;
        for (EGLint f : formats) {
            if (static_cast<uint32_t>(f) == target) {
                present = true;
                break;
            }
        }

        std::cout << fourcc(target) << " 0x" << std::hex << target << std::dec
                  << " advertised=" << (present ? "yes" : "no") << "\n";

        if (present && query_modifiers) {
            EGLint nmods = 0;
            if (query_modifiers(dpy, target, 0, nullptr, nullptr, &nmods) && nmods > 0) {
                std::vector<EGLuint64KHR> mods(nmods);
                std::vector<EGLBoolean> external_only(nmods);
                EGLint ngot = 0;
                if (query_modifiers(dpy, target, nmods, mods.data(), external_only.data(), &ngot)) {
                    for (EGLint i = 0; i < ngot; ++i) {
                        std::cout << "  modifier=0x" << std::hex << mods[i] << std::dec
                                  << " external_only=" << (external_only[i] ? "yes" : "no") << "\n";
                    }
                }
            } else {
                std::cout << "  modifiers: none reported\n";
            }
        }
    }

    eglTerminate(dpy);
    gbm_device_destroy(gbm);
    close(fd);
    return 0;
}
