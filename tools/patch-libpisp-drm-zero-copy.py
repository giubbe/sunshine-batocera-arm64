#!/usr/bin/env python3
from pathlib import Path

p = Path('src/examples/convert.cpp')
s = p.read_text()


def once(old: str, new: str):
    global s
    n = s.count(old)
    if n != 1:
        raise SystemExit(f'expected exactly one match, got {n}: {old[:80]!r}')
    s = s.replace(old, new, 1)

once('#include <linux/media.h>\n', '''#include <linux/media.h>\n#include <xf86drm.h>\n#include <xf86drmMode.h>\n#include <cerrno>\n#include <fcntl.h>\n#include <unistd.h>\n''')

once('{ "RGBX8888", { read_32, write_32 } },\n', '''{ "RGBX8888", { read_32, write_32 } },\n\t{ "XRGB8888", { read_32, write_32 } },\n''')

once('(\"h,help\", \"Print usage\")\n', '''("drm-fb-id", "Use DRM framebuffer ID as zero-copy PiSP input", cxxopts::value<unsigned int>())\n\t\t("drm-device", "DRM card used with --drm-fb-id", cxxopts::value<std::string>()->default_value("/dev/dri/card1"))\n\t\t("h,help", "Print usage")\n''')

needle = '''\tauto in_file = parse_format(args["input-format"].as<std::string>());\n\tif (!Formats.count(in_file.format))\n\t{\n\t\tstd::cerr << "Invalid input-format specified" << std::endl;\n\t\texit(-1);\n\t}\n'''
replacement = needle + '''\n\tint drm_fd = -1;\n\tdrmModeFB2Ptr drm_fb = nullptr;\n\tint drm_prime_fd = -1;\n\tsize_t drm_prime_size = 0;\n\n\tif (args.count("drm-fb-id"))\n\t{\n\t\tconst std::string drm_device = args["drm-device"].as<std::string>();\n\t\tconst uint32_t drm_fb_id = args["drm-fb-id"].as<unsigned int>();\n\t\tdrm_fd = open(drm_device.c_str(), O_RDWR | O_CLOEXEC);\n\t\tif (drm_fd < 0)\n\t\t{\n\t\t\tstd::cerr << "DRM open failed: " << strerror(errno) << std::endl;\n\t\t\texit(-1);\n\t\t}\n\n\t\tdrm_fb = drmModeGetFB2(drm_fd, drm_fb_id);\n\t\tif (!drm_fb)\n\t\t{\n\t\t\tstd::cerr << "drmModeGetFB2 failed: " << strerror(errno) << std::endl;\n\t\t\texit(-1);\n\t\t}\n\n\t\tchar fourcc[5] = {\n\t\t\t(char)(drm_fb->pixel_format & 0xff),\n\t\t\t(char)((drm_fb->pixel_format >> 8) & 0xff),\n\t\t\t(char)((drm_fb->pixel_format >> 16) & 0xff),\n\t\t\t(char)((drm_fb->pixel_format >> 24) & 0xff), 0 };\n\n\t\tstd::cerr << "DRM framebuffer: id=" << drm_fb_id\n\t\t\t\t  << " size=" << drm_fb->width << "x" << drm_fb->height\n\t\t\t\t  << " fourcc=" << fourcc\n\t\t\t\t  << " pitch=" << drm_fb->pitches[0]\n\t\t\t\t  << " offset=" << drm_fb->offsets[0]\n\t\t\t\t  << " modifier=0x" << std::hex << drm_fb->modifier << std::dec\n\t\t\t\t  << std::endl;\n\n\t\tif (std::string(fourcc) != "XR24")\n\t\t{\n\t\t\tstd::cerr << "Probe currently requires XR24/XRGB8888 input" << std::endl;\n\t\t\texit(-1);\n\t\t}\n\t\tif (drm_fb->modifier != 0 || drm_fb->offsets[0] != 0)\n\t\t{\n\t\t\tstd::cerr << "Probe currently requires LINEAR modifier and zero offset" << std::endl;\n\t\t\texit(-1);\n\t\t}\n\t\tif (drmPrimeHandleToFD(drm_fd, drm_fb->handles[0], DRM_CLOEXEC | DRM_RDWR, &drm_prime_fd))\n\t\t{\n\t\t\tstd::cerr << "drmPrimeHandleToFD failed: " << strerror(errno) << std::endl;\n\t\t\texit(-1);\n\t\t}\n\t\toff_t end = lseek(drm_prime_fd, 0, SEEK_END);\n\t\tif (end <= 0)\n\t\t{\n\t\t\tstd::cerr << "Unable to determine DMA-BUF size" << std::endl;\n\t\t\texit(-1);\n\t\t}\n\t\tdrm_prime_size = static_cast<size_t>(end);\n\n\t\tin_file.width = drm_fb->width;\n\t\tin_file.height = drm_fb->height;\n\t\tin_file.stride = drm_fb->pitches[0];\n\t\tin_file.format = "XRGB8888";\n\t\tstd::cerr << "DRM PRIME export OK: fd=" << drm_prime_fd\n\t\t\t\t  << " size=" << drm_prime_size << std::endl;\n\t}\n'''
once(needle, replacement)

once('''\tif (in_file.format == "RGBX8888" && !variant->BackendRGB32Supported(0))\n\t{\n\t\tstd::cerr << "Backend hardware does not support RGBX input" << std::endl;\n\t\texit(-1);\n\t}\n''', '''\tif ((in_file.format == "RGBX8888" || in_file.format == "XRGB8888") && !variant->BackendRGB32Supported(0))\n\t{\n\t\tstd::cerr << "Backend reports RGB32 input unsupported; continuing only for XRGB8888 DRM probe" << std::endl;\n\t\tif (in_file.format != "XRGB8888" || !args.count("drm-fb-id"))\n\t\t\texit(-1);\n\t}\n''')

once('''\tlibpisp::compute_optimal_stride(i);\n\tbe.SetInputFormat(i);\n''', '''\tlibpisp::compute_optimal_stride(i);\n\tif (args.count("drm-fb-id"))\n\t\ti.stride = in_file.stride;\n\tbe.SetInputFormat(i);\n''')

once('''\tbackend_device.Setup(config);\n\tauto buffers = backend_device.GetBufferSlice();\n\n\tstd::string input_filename = args["input"].as<std::string>();\n''', '''\tbackend_device.Setup(config);\n\tauto buffers = backend_device.GetBufferSlice();\n\n\tstd::map<std::string, Buffer> run_buffers;\n\tfor (const auto &[name, ref] : buffers)\n\t\trun_buffers.emplace(name, ref.get());\n\n\tif (args.count("drm-fb-id"))\n\t{\n\t\tstd::array<int, 3> fds = { dup(drm_prime_fd), -1, -1 };\n\t\tstd::array<size_t, 3> sizes = { drm_prime_size, 0, 0 };\n\t\tif (fds[0] < 0)\n\t\t{\n\t\t\tstd::cerr << "dup(DMA-BUF) failed: " << strerror(errno) << std::endl;\n\t\t\texit(-1);\n\t\t}\n\t\trun_buffers.at("pispbe-input") = Buffer(fds, sizes);\n\t\tstd::cerr << "PiSP input replaced with external DRM DMA-BUF" << std::endl;\n\t}\n\n\tstd::string input_filename = args.count("input") ? args["input"].as<std::string>() : std::string();\n''')

once('''\tstd::ifstream in(input_filename, std::ios::binary);\n\tif (!in.is_open())\n\t{\n\t\tstd::cerr << "Unable to open input file" << std::endl;\n\t\texit(-1);\n\t}\n''', '''\tstd::ifstream in;\n\tif (!args.count("drm-fb-id"))\n\t{\n\t\tin.open(input_filename, std::ios::binary);\n\t\tif (!in.is_open())\n\t\t{\n\t\t\tstd::cerr << "Unable to open input file" << std::endl;\n\t\t\texit(-1);\n\t\t}\n\t}\n''')

once('''\t{\n\t\tBuffer::Sync input(buffers.at("pispbe-input"), Buffer::Sync::Access::ReadWrite);\n\t\tFormats.at(in_file.format).read_file(input.Get(), in, in_file.width, in_file.height, in_file.stride, i.stride);\n\t\tin.close();\n\t}\n\n\tint ret = backend_device.Run(buffers);\n''', '''\tif (!args.count("drm-fb-id"))\n\t{\n\t\tBuffer::Sync input(run_buffers.at("pispbe-input"), Buffer::Sync::Access::ReadWrite);\n\t\tFormats.at(in_file.format).read_file(input.Get(), in, in_file.width, in_file.height, in_file.stride, i.stride);\n\t\tin.close();\n\t}\n\n\tstd::cerr << "Running PiSP job..." << std::endl;\n\tint ret = backend_device.Run(run_buffers);\n''')

once('''\t\tBuffer::Sync output(buffers.at("pispbe-output0"), Buffer::Sync::Access::Read);\n''', '''\t\tBuffer::Sync output(run_buffers.at("pispbe-output0"), Buffer::Sync::Access::Read);\n''')

# Clean up only after output has been consumed.
once('''\treturn 0;\n}\n''', '''\tif (drm_prime_fd >= 0) close(drm_prime_fd);\n\tif (drm_fb) drmModeFreeFB2(drm_fb);\n\tif (drm_fd >= 0) close(drm_fd);\n\treturn 0;\n}\n''')

p.write_text(s)

m = Path('src/examples/meson.build')
ms = m.read_text()
old = "dependencies: [libpisp_dep, opts_dep],"
if ms.count(old) != 1:
    raise SystemExit('unexpected examples/meson.build dependency stanza')
ms = ms.replace("opts_dep = dependency('cxxopts', fallback : ['cxxopts', 'cxxopts_dep'])\n",
                "opts_dep = dependency('cxxopts', fallback : ['cxxopts', 'cxxopts_dep'])\ndrm_dep = dependency('libdrm')\n", 1)
ms = ms.replace(old, "dependencies: [libpisp_dep, opts_dep, drm_dep],", 1)
m.write_text(ms)
