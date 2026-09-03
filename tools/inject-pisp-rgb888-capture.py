#!/usr/bin/env python3
from pathlib import Path

video = Path("source/src/video.cpp")
kms = Path("source/src/platform/linux/kmsgrab.cpp")

vs = video.read_text()
ks = kms.read_text()

def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label} anchor count={count}")
    return text.replace(old, new, 1)

kms_readback_old = '''        gl::ctx.GetTextureSubImage(rgb->tex[0], 0, img_offset_x, img_offset_y, 0, width, height, 1, GL_BGRA, GL_UNSIGNED_BYTE, img_out->height * img_out->row_pitch, img_out->data);
'''
kms_readback_new = '''        gl::ctx.GetTextureSubImage(rgb->tex[0], 0, img_offset_x, img_offset_y, 0, width, height, 1, GL_RGB, GL_UNSIGNED_BYTE, img_out->height * img_out->row_pitch, img_out->data);
'''
ks = replace_once(ks, kms_readback_old, kms_readback_new, "kms GL_RGB readback")

kms_alloc_old = '''        img->pixel_pitch = 4;
        img->row_pitch = img->pixel_pitch * width;
        img->data = new std::uint8_t[height * img->row_pitch];
'''
kms_alloc_new = '''        img->pixel_pitch = 3;
        img->row_pitch = img->pixel_pitch * width;
        img->data = new std::uint8_t[height * img->row_pitch];
'''
ks = replace_once(ks, kms_alloc_old, kms_alloc_new, "kms RGB888 allocation")

kms_cursor_old = '''          auto pixels_begin = &pixels[(y + cursor_y) * (img.row_pitch / img.pixel_pitch) + cursor_x];

          std::for_each(cursor_begin, cursor_end, [&](uint32_t cursor_pixel) {
            auto colors_in = (uint8_t *) pixels_begin;
            auto alpha = (*(uint *) &cursor_pixel) >> 24u;
            if (alpha == 255) {
              *pixels_begin = cursor_pixel;
            } else {
              auto colors_out = (uint8_t *) &cursor_pixel;
              colors_in[0] = colors_out[0] + (colors_in[0] * (255 - alpha) + 255 / 2) / 255;
              colors_in[1] = colors_out[1] + (colors_in[1] * (255 - alpha) + 255 / 2) / 255;
              colors_in[2] = colors_out[2] + (colors_in[2] * (255 - alpha) + 255 / 2) / 255;
            }
            ++pixels_begin;
          });
'''
kms_cursor_new = '''          if (img.pixel_pitch == 3) {
            auto rgb_pixel = img.data + static_cast<size_t>(y + cursor_y) * img.row_pitch + static_cast<size_t>(cursor_x) * 3;

            std::for_each(cursor_begin, cursor_end, [&](uint32_t cursor_pixel) {
              auto colors_out = (uint8_t *) &cursor_pixel;
              auto alpha = (*(uint *) &cursor_pixel) >> 24u;

              // KMS cursor pixels are ARGB8888 in memory (B,G,R,A bytes on
              // little-endian systems). GL_RGB capture is R,G,B.
              if (alpha == 255) {
                rgb_pixel[0] = colors_out[2];
                rgb_pixel[1] = colors_out[1];
                rgb_pixel[2] = colors_out[0];
              } else {
                rgb_pixel[0] = colors_out[2] + (rgb_pixel[0] * (255 - alpha) + 255 / 2) / 255;
                rgb_pixel[1] = colors_out[1] + (rgb_pixel[1] * (255 - alpha) + 255 / 2) / 255;
                rgb_pixel[2] = colors_out[0] + (rgb_pixel[2] * (255 - alpha) + 255 / 2) / 255;
              }

              rgb_pixel += 3;
            });
          } else {
            auto pixels_begin = &pixels[(y + cursor_y) * (img.row_pitch / img.pixel_pitch) + cursor_x];

            std::for_each(cursor_begin, cursor_end, [&](uint32_t cursor_pixel) {
              auto colors_in = (uint8_t *) pixels_begin;
              auto alpha = (*(uint *) &cursor_pixel) >> 24u;
              if (alpha == 255) {
                *pixels_begin = cursor_pixel;
              } else {
                auto colors_out = (uint8_t *) &cursor_pixel;
                colors_in[0] = colors_out[0] + (colors_in[0] * (255 - alpha) + 255 / 2) / 255;
                colors_in[1] = colors_out[1] + (colors_in[1] * (255 - alpha) + 255 / 2) / 255;
                colors_in[2] = colors_out[2] + (colors_in[2] * (255 - alpha) + 255 / 2) / 255;
              }
              ++pixels_begin;
            });
          }
'''
ks = replace_once(ks, kms_cursor_old, kms_cursor_new, "kms RGB888 cursor blend")

kms_init_old = '''        ctx = std::move(*ctx_opt);

        return 0;
'''
kms_init_new = '''        ctx = std::move(*ctx_opt);
        gl::ctx.PixelStorei(GL_PACK_ALIGNMENT, 1);

        BOOST_LOG(info) << "PISP_RGB888_CAPTURE enabled: KMS RAM readback uses GL_RGB/RGB888"sv;
        return 0;
'''
ks = replace_once(ks, kms_init_old, kms_init_new, "kms RGB888 marker")

video_sws_input_old = '''      // Setup the input frame using the caller's img_t
      sws_input_frame->data[0] = img.data;
      sws_input_frame->linesize[0] = img.row_pitch;
'''
video_sws_input_new = '''      // Setup the input frame using the caller's img_t. The experimental
      // KMS RGB888 capture is normally consumed by PiSP before reaching this
      // point. If PiSP falls back, reconstruct BGR0 so libswscale retains the
      // exact upstream input contract.
#ifdef __aarch64__
      if (img.pixel_pitch == 3) {
        const size_t fallback_stride = static_cast<size_t>(img.width) * 4;
        const size_t fallback_size = fallback_stride * img.height;
        if (pisp_fallback_bgr0_size_ < fallback_size) {
          pisp_fallback_bgr0_ = std::make_unique<uint8_t[]>(fallback_size);
          pisp_fallback_bgr0_size_ = fallback_size;
        }

        rgb888_to_bgr0(img.data, img.row_pitch, pisp_fallback_bgr0_.get(), static_cast<int>(fallback_stride), img.width, img.height);
        sws_input_frame->data[0] = pisp_fallback_bgr0_.get();
        sws_input_frame->linesize[0] = static_cast<int>(fallback_stride);
      } else
#endif
      {
        sws_input_frame->data[0] = img.data;
        sws_input_frame->linesize[0] = img.row_pitch;
      }
'''
vs = replace_once(vs, video_sws_input_old, video_sws_input_new, "video RGB888 swscale fallback")

video_bgr_func_anchor = '''    static void bgr0_to_rgb888(const uint8_t *__restrict src, int src_stride, uint8_t *__restrict dst, unsigned int dst_stride, int width, int height) {
'''
video_helpers = '''    static void copy_rgb888(const uint8_t *__restrict src, int src_stride, uint8_t *__restrict dst, unsigned int dst_stride, int width, int height) {
      const size_t row_bytes = static_cast<size_t>(width) * 3;
      if (src_stride == static_cast<int>(row_bytes) && dst_stride == row_bytes) {
        std::memcpy(dst, src, row_bytes * height);
        return;
      }

      for (int y = 0; y < height; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * dst_stride,
                    src + static_cast<ptrdiff_t>(y) * src_stride,
                    row_bytes);
      }
    }

    static void rgb888_to_bgr0(const uint8_t *__restrict src, int src_stride, uint8_t *__restrict dst, int dst_stride, int width, int height) {
      for (int y = 0; y < height; ++y) {
        const uint8_t *s = src + static_cast<ptrdiff_t>(y) * src_stride;
        uint8_t *d = dst + static_cast<ptrdiff_t>(y) * dst_stride;
        for (int x = 0; x < width; ++x) {
          const uint8_t *sp = s + static_cast<size_t>(x) * 3;
          uint8_t *dp = d + static_cast<size_t>(x) * 4;
          dp[0] = sp[2];
          dp[1] = sp[1];
          dp[2] = sp[0];
          dp[3] = 0;
        }
      }
    }

''' + video_bgr_func_anchor
vs = replace_once(vs, video_bgr_func_anchor, video_helpers, "video RGB888 helpers")

video_pisp_copy_old = '''          bgr0_to_rgb888(img.data, img.row_pitch, mem[0], pisp_in_stride_, pisp_in_width_, pisp_in_height_);
'''
video_pisp_copy_new = '''          if (img.pixel_pitch == 3) {
            copy_rgb888(img.data, img.row_pitch, mem[0], pisp_in_stride_, pisp_in_width_, pisp_in_height_);
            if (!pisp_rgb888_capture_logged_) {
              BOOST_LOG(info) << "PISP_RGB888_CAPTURE direct path active: bypassing BGR0->RGB888 NEON repack"sv;
              pisp_rgb888_capture_logged_ = true;
            }
          } else if (img.pixel_pitch == 4) {
            bgr0_to_rgb888(img.data, img.row_pitch, mem[0], pisp_in_stride_, pisp_in_width_, pisp_in_height_);
          } else {
            BOOST_LOG(warning) << "PISP_RGB888_CAPTURE unsupported capture pixel pitch "sv << img.pixel_pitch;
            return -1;
          }
'''
vs = replace_once(vs, video_pisp_copy_old, video_pisp_copy_new, "video PiSP RGB888 direct input")

video_members_old = '''    bool pisp_enabled_ {false};
    std::unique_ptr<libpisp::helpers::BackendDevice> pisp_backend_;
'''
video_members_new = '''    bool pisp_enabled_ {false};
    bool pisp_rgb888_capture_logged_ {false};
    std::unique_ptr<uint8_t[]> pisp_fallback_bgr0_;
    size_t pisp_fallback_bgr0_size_ {0};
    std::unique_ptr<libpisp::helpers::BackendDevice> pisp_backend_;
'''
vs = replace_once(vs, video_members_old, video_members_new, "video RGB888 members")

kms.write_text(ks)
video.write_text(vs)
