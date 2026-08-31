// SPDX-License-Identifier: MIT
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <set>
#include <string>
#include <unistd.h>

static std::string fourcc_to_string(uint32_t f)
{
    char s[5] = {
        static_cast<char>(f & 0xff),
        static_cast<char>((f >> 8) & 0xff),
        static_cast<char>((f >> 16) & 0xff),
        static_cast<char>((f >> 24) & 0xff),
        '\0'
    };
    return std::string(s, 4);
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <fb-id> [drm-device]\n"
                  << "Example: " << argv[0] << " 689 /dev/dri/card1\n";
        return 2;
    }

    char *end = nullptr;
    errno = 0;
    unsigned long parsed = std::strtoul(argv[1], &end, 0);
    if (errno || !end || *end != '\0' || parsed > UINT32_MAX) {
        std::cerr << "Invalid framebuffer id: " << argv[1] << "\n";
        return 2;
    }
    const uint32_t fb_id = static_cast<uint32_t>(parsed);
    const char *device = argc == 3 ? argv[2] : "/dev/dri/card1";

    int fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        std::cerr << "open(" << device << ") failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    drmVersionPtr ver = drmGetVersion(fd);
    if (ver) {
        std::cout << "device=" << device
                  << " driver=" << std::string(ver->name, ver->name_len)
                  << " version=" << ver->version_major << '.' << ver->version_minor << '.' << ver->version_patchlevel
                  << "\n";
        drmFreeVersion(ver);
    } else {
        std::cout << "device=" << device << " driver=<unknown>\n";
    }

    drmModeFB2Ptr fb = drmModeGetFB2(fd, fb_id);
    if (!fb) {
        std::cerr << "drmModeGetFB2(" << fb_id << ") failed: " << std::strerror(errno)
                  << " (errno=" << errno << ")\n";
        close(fd);
        return 1;
    }

    const std::string fourcc = fourcc_to_string(fb->pixel_format);
    std::cout << "fb_id=" << fb->fb_id
              << " width=" << fb->width
              << " height=" << fb->height
              << " fourcc=" << fourcc
              << " fourcc_hex=0x" << std::hex << fb->pixel_format << std::dec
              << " flags=0x" << std::hex << fb->flags << std::dec
              << " modifier=0x" << std::hex << fb->modifier << std::dec
              << "\n";

    std::set<uint32_t> exported_handles;
    int exported_count = 0;

    for (unsigned i = 0; i < 4; ++i) {
        if (!fb->handles[i] && !fb->pitches[i] && !fb->offsets[i])
            continue;

        std::cout << "plane=" << i
                  << " handle=" << fb->handles[i]
                  << " pitch=" << fb->pitches[i]
                  << " offset=" << fb->offsets[i];

        if (fb->handles[i] == 0) {
            std::cout << " dmabuf=<no-handle>\n";
            continue;
        }

        // The same GEM handle can back multiple image planes. Export each unique handle once.
        if (!exported_handles.insert(fb->handles[i]).second) {
            std::cout << " dmabuf=<same-handle-as-earlier-plane>\n";
            continue;
        }

        int prime_fd = -1;
        errno = 0;
        int ret = drmPrimeHandleToFD(fd, fb->handles[i], DRM_CLOEXEC | DRM_RDWR, &prime_fd);
        if (ret != 0) {
            const int saved = errno;
            std::cout << " dmabuf_export=FAILED errno=" << saved
                      << " error=\"" << std::strerror(saved) << "\"\n";
        } else {
            off_t size = lseek(prime_fd, 0, SEEK_END);
            std::cout << " dmabuf_export=OK fd=" << prime_fd;
            if (size >= 0)
                std::cout << " size=" << static_cast<long long>(size);
            else
                std::cout << " size=<unknown>";
            std::cout << "\n";
            ++exported_count;
            close(prime_fd);
        }
    }

    std::cout << "summary fourcc=" << fourcc
              << " modifier=0x" << std::hex << fb->modifier << std::dec
              << " exported_unique_dmabufs=" << exported_count
              << "\n";

    drmModeFreeFB2(fb);
    close(fd);
    return 0;
}
