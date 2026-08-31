#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using GetTextureSubImageFn = void (*)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei,
                                      GLenum, GLenum, GLsizei, void *);

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr int kWarmup = 20;
constexpr int kRuns = 300;

const char *gl_error_name(GLenum err) {
  switch (err) {
    case GL_NO_ERROR: return "GL_NO_ERROR";
    case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
    case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
    default: return "unknown";
  }
}

bool clear_errors() {
  bool had_error = false;
  while (glGetError() != GL_NO_ERROR) had_error = true;
  return had_error;
}

struct BenchResult {
  std::string name;
  GLenum format;
  int bytes_per_pixel;
  bool supported = false;
  GLenum gl_error = GL_NO_ERROR;
  double total_ms = 0.0;
  double avg_ms = 0.0;
  double fps = 0.0;
  std::uint64_t checksum = 0;
};

BenchResult bench(GetTextureSubImageFn get_texture_sub_image,
                  GLuint texture,
                  const char *name,
                  GLenum format,
                  int bytes_per_pixel) {
  BenchResult r;
  r.name = name;
  r.format = format;
  r.bytes_per_pixel = bytes_per_pixel;

  const std::size_t size = static_cast<std::size_t>(kWidth) * kHeight * bytes_per_pixel;
  std::vector<std::uint8_t> out(size, 0);

  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  clear_errors();

  get_texture_sub_image(texture, 0, 0, 0, 0, kWidth, kHeight, 1,
                        format, GL_UNSIGNED_BYTE, static_cast<GLsizei>(out.size()), out.data());
  GLenum err = glGetError();
  if (err != GL_NO_ERROR) {
    r.gl_error = err;
    return r;
  }

  for (int i = 0; i < kWarmup; ++i) {
    get_texture_sub_image(texture, 0, 0, 0, 0, kWidth, kHeight, 1,
                          format, GL_UNSIGNED_BYTE, static_cast<GLsizei>(out.size()), out.data());
  }
  glFinish();

  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < kRuns; ++i) {
    get_texture_sub_image(texture, 0, 0, 0, 0, kWidth, kHeight, 1,
                          format, GL_UNSIGNED_BYTE, static_cast<GLsizei>(out.size()), out.data());
  }
  glFinish();
  const auto t1 = std::chrono::steady_clock::now();

  err = glGetError();
  if (err != GL_NO_ERROR) {
    r.gl_error = err;
    return r;
  }

  r.supported = true;
  r.total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  r.avg_ms = r.total_ms / kRuns;
  r.fps = 1000.0 / r.avg_ms;

  // Touch bytes across the entire result so a successful transfer is observable.
  // This is outside the timed region.
  constexpr std::size_t stride = 4093;
  for (std::size_t i = 0; i < out.size(); i += stride) {
    r.checksum = (r.checksum * 1315423911ULL) ^ out[i];
  }

  return r;
}

void print_result(const BenchResult &r) {
  std::cout << std::left << std::setw(8) << r.name << " ";
  if (!r.supported) {
    std::cout << "UNSUPPORTED gl_error=0x" << std::hex << r.gl_error << std::dec
              << " (" << gl_error_name(r.gl_error) << ")\n";
    return;
  }

  const double mib = static_cast<double>(kWidth) * kHeight * r.bytes_per_pixel / (1024.0 * 1024.0);
  const double gib_s = (mib / 1024.0) / (r.avg_ms / 1000.0);

  std::cout << std::fixed << std::setprecision(3)
            << "runs=" << kRuns
            << " warmup=" << kWarmup
            << " frame_MiB=" << mib
            << " total_ms=" << r.total_ms
            << " avg_ms=" << r.avg_ms
            << " fps=" << r.fps
            << " GiB/s=" << gib_s
            << " checksum=0x" << std::hex << r.checksum << std::dec
            << "\n";
}

}  // namespace

int main() {
  auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
      eglGetProcAddress("eglGetPlatformDisplayEXT"));

  EGLDisplay display = EGL_NO_DISPLAY;
  if (get_platform_display) {
#ifdef EGL_PLATFORM_SURFACELESS_MESA
    display = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
#endif
  }
  if (display == EGL_NO_DISPLAY) {
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  }
  if (display == EGL_NO_DISPLAY) {
    std::cerr << "FAIL: eglGetDisplay/eglGetPlatformDisplayEXT\n";
    return 2;
  }

  EGLint egl_major = 0, egl_minor = 0;
  if (!eglInitialize(display, &egl_major, &egl_minor)) {
    std::cerr << "FAIL: eglInitialize error=0x" << std::hex << eglGetError() << std::dec << "\n";
    return 2;
  }

  if (!eglBindAPI(EGL_OPENGL_API)) {
    std::cerr << "FAIL: eglBindAPI(EGL_OPENGL_API) error=0x" << std::hex << eglGetError() << std::dec << "\n";
    eglTerminate(display);
    return 2;
  }

  const EGLint cfg_attr[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_NONE};

  EGLConfig config = nullptr;
  EGLint num_configs = 0;
  if (!eglChooseConfig(display, cfg_attr, &config, 1, &num_configs) || num_configs < 1) {
    std::cerr << "FAIL: eglChooseConfig error=0x" << std::hex << eglGetError() << std::dec << "\n";
    eglTerminate(display);
    return 2;
  }

  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, nullptr);
  if (context == EGL_NO_CONTEXT) {
    std::cerr << "FAIL: eglCreateContext error=0x" << std::hex << eglGetError() << std::dec << "\n";
    eglTerminate(display);
    return 2;
  }

  if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
    // Fallback for drivers that advertise surfaceless poorly.
    const EGLint pb_attr[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pb_attr);
    if (surface == EGL_NO_SURFACE || !eglMakeCurrent(display, surface, surface, context)) {
      std::cerr << "FAIL: eglMakeCurrent error=0x" << std::hex << eglGetError() << std::dec << "\n";
      eglDestroyContext(display, context);
      eglTerminate(display);
      return 2;
    }
  }

  const char *vendor = reinterpret_cast<const char *>(glGetString(GL_VENDOR));
  const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
  const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));

  std::cout << "EGL " << egl_major << "." << egl_minor << " vendor="
            << (eglQueryString(display, EGL_VENDOR) ? eglQueryString(display, EGL_VENDOR) : "?") << "\n";
  std::cout << "GL_VENDOR=" << (vendor ? vendor : "?") << "\n";
  std::cout << "GL_RENDERER=" << (renderer ? renderer : "?") << "\n";
  std::cout << "GL_VERSION=" << (version ? version : "?") << "\n";
  std::cout << "frame=" << kWidth << "x" << kHeight << "\n";

  auto get_texture_sub_image = reinterpret_cast<GetTextureSubImageFn>(
      eglGetProcAddress("glGetTextureSubImage"));
  if (!get_texture_sub_image) {
    std::cerr << "FAIL: glGetTextureSubImage unavailable\n";
    eglDestroyContext(display, context);
    eglTerminate(display);
    return 3;
  }

  std::vector<std::uint8_t> src(static_cast<std::size_t>(kWidth) * kHeight * 4);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const std::size_t p = (static_cast<std::size_t>(y) * kWidth + x) * 4;
      src[p + 0] = static_cast<std::uint8_t>((x + 17) & 0xff);      // R
      src[p + 1] = static_cast<std::uint8_t>((y + 31) & 0xff);      // G
      src[p + 2] = static_cast<std::uint8_t>((x + y + 47) & 0xff);  // B
      src[p + 3] = 255;
    }
  }

  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, src.data());
  glFinish();

  GLenum setup_err = glGetError();
  if (setup_err != GL_NO_ERROR) {
    std::cerr << "FAIL: texture setup gl_error=0x" << std::hex << setup_err << std::dec << "\n";
    glDeleteTextures(1, &texture);
    eglDestroyContext(display, context);
    eglTerminate(display);
    return 4;
  }

  // Sunshine's current software capture path requests GL_BGRA/GL_UNSIGNED_BYTE.
  const auto bgra = bench(get_texture_sub_image, texture, "BGRA32", GL_BGRA, 4);
  const auto rgba = bench(get_texture_sub_image, texture, "RGBA32", GL_RGBA, 4);
  const auto rgb = bench(get_texture_sub_image, texture, "RGB24", GL_RGB, 3);
#ifdef GL_BGR
  const auto bgr = bench(get_texture_sub_image, texture, "BGR24", GL_BGR, 3);
#endif

  print_result(bgra);
  print_result(rgba);
  print_result(rgb);
#ifdef GL_BGR
  print_result(bgr);
#endif

  if (bgra.supported && rgb.supported) {
    std::cout << std::fixed << std::setprecision(3)
              << "RGB24_vs_BGRA32=" << (rgb.avg_ms / bgra.avg_ms) << "x"
              << " delta_ms=" << (rgb.avg_ms - bgra.avg_ms) << "\n";
  }
#ifdef GL_BGR
  if (bgra.supported && bgr.supported) {
    std::cout << std::fixed << std::setprecision(3)
              << "BGR24_vs_BGRA32=" << (bgr.avg_ms / bgra.avg_ms) << "x"
              << " delta_ms=" << (bgr.avg_ms - bgra.avg_ms) << "\n";
  }
#endif

  glDeleteTextures(1, &texture);
  eglDestroyContext(display, context);
  eglTerminate(display);
  return (bgra.supported && (rgb.supported
#ifdef GL_BGR
                             || bgr.supported
#endif
                             )) ? 0 : 5;
}
