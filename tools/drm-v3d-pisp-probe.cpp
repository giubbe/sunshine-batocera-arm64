// SPDX-License-Identifier: MIT
// Diagnostic path: VC4 DRM framebuffer -> EGL/V3D -> PiSP-owned RGB888 DMA-BUF -> PiSP -> YUV420P.

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "helpers/backend_device.hpp"
#include "helpers/media_device.hpp"
#include "libpisp/backend/backend.hpp"
#include "libpisp/common/logging.hpp"
#include "libpisp/common/utils.hpp"
#include "libpisp/variants/variant.hpp"

using Buffer = libpisp::helpers::Buffer;

static void fail(const std::string &s)
{
    throw std::runtime_error(s);
}

static std::string fourcc_string(uint32_t f)
{
    char s[5] = { static_cast<char>(f & 0xff), static_cast<char>((f >> 8) & 0xff),
                  static_cast<char>((f >> 16) & 0xff), static_cast<char>((f >> 24) & 0xff), 0 };
    return std::string(s, 4);
}

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        fail(std::string("shader compile failed: ") + log);
    }
    return sh;
}

int main(int argc, char **argv)
{
    try {
        if (argc < 2 || argc > 4) {
            std::cerr << "Usage: " << argv[0] << " <fb-id> [drm-card=/dev/dri/card1] [render-node=/dev/dri/renderD128]\n";
            return 2;
        }
        const uint32_t fb_id = static_cast<uint32_t>(std::stoul(argv[1]));
        const char *drm_card = argc >= 3 ? argv[2] : "/dev/dri/card1";
        const char *render_node = argc >= 4 ? argv[3] : "/dev/dri/renderD128";

        libpisp::logging_init();

        // Configure PiSP first. Its RGB888 input allocation becomes the GPU render target.
        libpisp::helpers::MediaDevice devices;
        std::string media_dev = devices.Acquire();
        if (media_dev.empty()) fail("Unable to acquire a pisp_be device");
        std::cerr << "Acquired PiSP device " << media_dev << "\n";

        const std::vector<libpisp::PiSPVariant> &variants = libpisp::get_variants();
        const media_device_info info = devices.DeviceInfo(media_dev);
        auto variant = std::find_if(variants.begin(), variants.end(),
                                    [&info](const auto &v) { return v.BackEndVersion() == info.hw_revision; });
        if (variant == variants.end()) fail("Unable to identify PiSP backend revision");

        libpisp::BackEnd be(libpisp::BackEnd::Config({}), *variant);
        pisp_be_global_config global {};
        be.GetGlobal(global);
        global.bayer_enables = 0;
        global.rgb_enables = PISP_BE_RGB_ENABLE_INPUT + PISP_BE_RGB_ENABLE_OUTPUT0;

        pisp_image_format_config in_fmt {};
        in_fmt.width = 1920;
        in_fmt.height = 1080;
        in_fmt.format = libpisp::get_pisp_image_format("RGB888");
        if (!in_fmt.format) fail("libpisp has no RGB888 format");
        libpisp::compute_optimal_stride(in_fmt);
        be.SetInputFormat(in_fmt);

        pisp_be_output_format_config out_fmt {};
        out_fmt.image.width = 1280;
        out_fmt.image.height = 720;
        out_fmt.image.format = libpisp::get_pisp_image_format("YUV420P");
        if (!out_fmt.image.format) fail("libpisp has no YUV420P format");
        libpisp::compute_optimal_stride(out_fmt.image, true);
        be.SetOutputFormat(0, out_fmt);

        pisp_be_ccm_config csc {};
        be.InitialiseYcbcr(csc, "jpeg");
        be.SetCsc(0, csc);
        global.rgb_enables |= PISP_BE_RGB_ENABLE_CSC0;
        be.SetGlobal(global);
        be.SetCrop(0, { 0, 0, in_fmt.width, in_fmt.height });
        be.SetSmartResize(0, { out_fmt.image.width, out_fmt.image.height });

        pisp_be_tiles_config config {};
        be.Prepare(&config);
        libpisp::helpers::BackendDevice backend(media_dev);
        backend.Setup(config);
        auto buffers = backend.GetBufferSlice();
        const Buffer &pisp_input = buffers.at("pispbe-input").get();
        std::cerr << "PiSP RGB888 input: fd=" << pisp_input.Fd()[0]
                  << " size=" << pisp_input.Size()[0]
                  << " stride=" << in_fmt.stride << "\n";

        if (pisp_input.Fd()[0] < 0) fail("PiSP input DMA-BUF fd is invalid");
        const size_t required_rgb = static_cast<size_t>(in_fmt.stride) * in_fmt.height;
        if (pisp_input.Size()[0] < required_rgb) fail("PiSP RGB888 input allocation is smaller than stride*height");

        // Export active VC4 framebuffer as PRIME DMA-BUF.
        int card_fd = open(drm_card, O_RDWR | O_CLOEXEC);
        if (card_fd < 0) fail(std::string("open DRM card failed: ") + strerror(errno));
        drmModeFB2Ptr fb = drmModeGetFB2(card_fd, fb_id);
        if (!fb) fail(std::string("drmModeGetFB2 failed: ") + strerror(errno));
        std::cerr << "DRM framebuffer: id=" << fb_id << " " << fb->width << "x" << fb->height
                  << " fourcc=" << fourcc_string(fb->pixel_format)
                  << " pitch=" << fb->pitches[0] << " offset=" << fb->offsets[0]
                  << " modifier=0x" << std::hex << fb->modifier << std::dec << "\n";
        if (fb->width != 1920 || fb->height != 1080 || fourcc_string(fb->pixel_format) != "XR24" ||
            fb->offsets[0] != 0 || fb->modifier != 0)
            fail("Probe requires the verified 1920x1080 XR24 LINEAR framebuffer with offset 0");

        int src_prime_fd = -1;
        if (drmPrimeHandleToFD(card_fd, fb->handles[0], DRM_CLOEXEC | DRM_RDWR, &src_prime_fd))
            fail(std::string("drmPrimeHandleToFD failed: ") + strerror(errno));
        std::cerr << "DRM PRIME source fd=" << src_prime_fd << "\n";

        // Create a surfaceless GLES context on V3D through the render node.
        int render_fd = open(render_node, O_RDWR | O_CLOEXEC);
        if (render_fd < 0) fail(std::string("open render node failed: ") + strerror(errno));
        gbm_device *gbm = gbm_create_device(render_fd);
        if (!gbm) fail("gbm_create_device failed");

        auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (!get_platform_display) fail("eglGetPlatformDisplayEXT unavailable");
        EGLDisplay dpy = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, nullptr);
        if (dpy == EGL_NO_DISPLAY) fail("EGL GBM display creation failed");
        EGLint emaj = 0, emin = 0;
        if (!eglInitialize(dpy, &emaj, &emin)) fail("eglInitialize failed");
        if (!eglBindAPI(EGL_OPENGL_ES_API)) fail("eglBindAPI GLES failed");

        const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
        EGLConfig egl_cfg = nullptr;
        EGLint ncfg = 0;
        if (!eglChooseConfig(dpy, cfg_attr, &egl_cfg, 1, &ncfg) || ncfg < 1) fail("eglChooseConfig failed");
        const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        EGLContext ctx = eglCreateContext(dpy, egl_cfg, EGL_NO_CONTEXT, ctx_attr);
        if (ctx == EGL_NO_CONTEXT) fail("eglCreateContext failed");
        if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) fail("surfaceless eglMakeCurrent failed");

        std::cerr << "EGL " << emaj << '.' << emin
                  << " vendor=" << (eglQueryString(dpy, EGL_VENDOR) ?: "?") << "\n";
        std::cerr << "GL renderer=" << (reinterpret_cast<const char *>(glGetString(GL_RENDERER)) ?: "?")
                  << " version=" << (reinterpret_cast<const char *>(glGetString(GL_VERSION)) ?: "?") << "\n";

        auto create_image = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
        auto destroy_image = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
        auto image_target_tex = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
        if (!create_image || !destroy_image || !image_target_tex) fail("Required EGLImage functions unavailable");

        const EGLint src_attrs[] = {
            EGL_WIDTH, static_cast<EGLint>(fb->width), EGL_HEIGHT, static_cast<EGLint>(fb->height),
            EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLint>(fb->pixel_format),
            EGL_DMA_BUF_PLANE0_FD_EXT, src_prime_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, static_cast<EGLint>(fb->offsets[0]),
            EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLint>(fb->pitches[0]), EGL_NONE
        };
        EGLImageKHR src_image = create_image(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, src_attrs);
        if (src_image == EGL_NO_IMAGE_KHR) fail("EGL import of XR24 source DMA-BUF failed");

        // Import PiSP's own RGB888 input DMA-BUF as the V3D destination.
        const EGLint dst_attrs[] = {
            EGL_WIDTH, static_cast<EGLint>(in_fmt.width), EGL_HEIGHT, static_cast<EGLint>(in_fmt.height),
            EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLint>(DRM_FORMAT_RGB888),
            EGL_DMA_BUF_PLANE0_FD_EXT, pisp_input.Fd()[0],
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLint>(in_fmt.stride), EGL_NONE
        };
        EGLImageKHR dst_image = create_image(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, dst_attrs);
        if (dst_image == EGL_NO_IMAGE_KHR) fail("EGL import of PiSP RGB888 destination DMA-BUF failed");
        std::cerr << "EGL DMA-BUF imports OK (XR24 source, RGB888 PiSP destination)\n";

        GLuint src_tex = 0, dst_tex = 0, fbo = 0;
        glGenTextures(1, &src_tex);
        glBindTexture(GL_TEXTURE_2D, src_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        image_target_tex(GL_TEXTURE_2D, src_image);
        if (glGetError() != GL_NO_ERROR) fail("Binding XR24 EGLImage as source texture failed");

        glGenTextures(1, &dst_tex);
        glBindTexture(GL_TEXTURE_2D, dst_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        image_target_tex(GL_TEXTURE_2D, dst_image);
        if (glGetError() != GL_NO_ERROR) fail("Binding RGB888 EGLImage as destination texture failed");

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst_tex, 0);
        GLenum fbo_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fbo_status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "RGB888 destination FBO status=0x" << std::hex << fbo_status << std::dec << "\n";
            fail("PiSP RGB888 DMA-BUF is not renderable by this GLES driver");
        }
        std::cerr << "RGB888 PiSP DMA-BUF is V3D-renderable\n";

        const char *vs_src =
            "attribute vec2 p; attribute vec2 uv; varying vec2 v;"
            "void main(){ gl_Position=vec4(p,0.0,1.0); v=uv; }";
        const char *fs_src =
            "precision mediump float; varying vec2 v; uniform sampler2D s;"
            "void main(){ gl_FragColor=texture2D(s,v); }";
        GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
        GLint linked = 0; glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if (!linked) fail("shader program link failed");
        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "s"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src_tex);

        const GLfloat pos[] = { -1,-1, 1,-1, -1,1, 1,1 };
        const GLfloat uv[]  = { 0,0, 1,0, 0,1, 1,1 };
        GLint ap = glGetAttribLocation(prog, "p"), au = glGetAttribLocation(prog, "uv");
        glEnableVertexAttribArray(ap); glVertexAttribPointer(ap, 2, GL_FLOAT, GL_FALSE, 0, pos);
        glEnableVertexAttribArray(au); glVertexAttribPointer(au, 2, GL_FLOAT, GL_FALSE, 0, uv);
        glViewport(0, 0, in_fmt.width, in_fmt.height);

        auto t0 = std::chrono::steady_clock::now();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glFinish();
        auto t1 = std::chrono::steady_clock::now();
        if (glGetError() != GL_NO_ERROR) fail("V3D draw failed");
        const double gpu_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cerr << "V3D XR24->RGB888 render completed: " << gpu_ms << " ms\n";

        auto p0 = std::chrono::steady_clock::now();
        int ret = backend.Run(buffers);
        auto p1 = std::chrono::steady_clock::now();
        if (ret) fail("PiSP BackendDevice::Run failed");
        const double pisp_ms = std::chrono::duration<double, std::milli>(p1 - p0).count();
        std::cerr << "PiSP RGB888 1080p -> YUV420P 720p completed: " << pisp_ms << " ms\n";

        // Validation read only after the hardware path has completed; not part of the streaming design.
        Buffer::Sync out_sync(buffers.at("pispbe-output0"), Buffer::Sync::Access::Read);
        const auto &mem = out_sync.Get();
        uint64_t checksum = 0;
        for (unsigned p = 0; p < 3; ++p) {
            if (!mem[p]) continue;
            const size_t n = std::min<size_t>(buffers.at("pispbe-output0").get().Size()[p], 4096);
            for (size_t j = 0; j < n; ++j) checksum += mem[p][j];
        }
        std::cerr << "Validation checksum(first <=4KiB/plane)=" << checksum << "\n";
        std::cerr << "SUCCESS: DRM XR24 -> V3D RGB888 DMA-BUF -> PiSP YUV420P, no CPU framebuffer copy\n";

        glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &src_tex); glDeleteTextures(1, &dst_tex);
        glDeleteProgram(prog); glDeleteShader(vs); glDeleteShader(fs);
        destroy_image(dpy, src_image); destroy_image(dpy, dst_image);
        eglDestroyContext(dpy, ctx); eglTerminate(dpy);
        gbm_device_destroy(gbm);
        close(render_fd); close(src_prime_fd); drmModeFreeFB2(fb); close(card_fd);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
