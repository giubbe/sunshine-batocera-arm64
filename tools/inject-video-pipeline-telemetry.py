#!/usr/bin/env python3
from pathlib import Path
import sys

p = Path(sys.argv[1] if len(sys.argv) > 1 else "source/src/video.cpp")
s = p.read_text()

anchor1 = """    while (true) {\n      // Break out of the encoding loop if any of the following are true:\n"""
insert1 = """    // Diagnostic telemetry for sustained software-encoding tests on Batocera.\n    auto telemetry_window_start = std::chrono::steady_clock::now();\n    uint64_t telemetry_frames = 0;\n    uint64_t telemetry_convert_calls = 0;\n    uint64_t telemetry_age_samples = 0;\n    double telemetry_convert_sum_ms = 0.0;\n    double telemetry_convert_max_ms = 0.0;\n    double telemetry_encode_sum_ms = 0.0;\n    double telemetry_encode_max_ms = 0.0;\n    double telemetry_age_in_sum_ms = 0.0;\n    double telemetry_age_in_max_ms = 0.0;\n    double telemetry_age_out_sum_ms = 0.0;\n    double telemetry_age_out_max_ms = 0.0;\n\n    while (true) {\n      // Break out of the encoding loop if any of the following are true:\n"""

anchor2 = """        if (auto img = images->pop(max_frametime)) {\n          frame_timestamp = img->frame_timestamp;\n          if (session->convert(*img)) {\n            BOOST_LOG(error) << \"Could not convert image\"sv;\n            return;\n          }\n        } else if (!images->running()) {\n          break;\n        }\n"""
insert2 = """        if (auto img = images->pop(max_frametime)) {\n          frame_timestamp = img->frame_timestamp;\n\n          if (frame_timestamp) {\n            const auto age_in_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - *frame_timestamp).count();\n            telemetry_age_in_sum_ms += age_in_ms;\n            telemetry_age_in_max_ms = std::max(telemetry_age_in_max_ms, age_in_ms);\n            ++telemetry_age_samples;\n          }\n\n          const auto convert_start = std::chrono::steady_clock::now();\n          if (session->convert(*img)) {\n            BOOST_LOG(error) << \"Could not convert image\"sv;\n            return;\n          }\n          const auto convert_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - convert_start).count();\n          telemetry_convert_sum_ms += convert_ms;\n          telemetry_convert_max_ms = std::max(telemetry_convert_max_ms, convert_ms);\n          ++telemetry_convert_calls;\n        } else if (!images->running()) {\n          break;\n        }\n"""

anchor3 = """      if (encode(frame_nr++, *session, packets, channel_data, frame_timestamp)) {\n        BOOST_LOG(error) << \"Could not encode video packet\"sv;\n        return;\n      }\n\n      session->request_normal_frame();\n"""
insert3 = """      const auto encode_start = std::chrono::steady_clock::now();\n      if (encode(frame_nr++, *session, packets, channel_data, frame_timestamp)) {\n        BOOST_LOG(error) << \"Could not encode video packet\"sv;\n        return;\n      }\n      const auto encode_end = std::chrono::steady_clock::now();\n      const auto encode_ms = std::chrono::duration<double, std::milli>(encode_end - encode_start).count();\n      telemetry_encode_sum_ms += encode_ms;\n      telemetry_encode_max_ms = std::max(telemetry_encode_max_ms, encode_ms);\n      ++telemetry_frames;\n\n      if (frame_timestamp) {\n        const auto age_out_ms = std::chrono::duration<double, std::milli>(encode_end - *frame_timestamp).count();\n        telemetry_age_out_sum_ms += age_out_ms;\n        telemetry_age_out_max_ms = std::max(telemetry_age_out_max_ms, age_out_ms);\n      }\n\n      const auto telemetry_elapsed = encode_end - telemetry_window_start;\n      if (telemetry_elapsed >= 1s) {\n        const auto elapsed_s = std::chrono::duration<double>(telemetry_elapsed).count();\n        const auto convert_avg_ms = telemetry_convert_calls ? telemetry_convert_sum_ms / telemetry_convert_calls : 0.0;\n        const auto encode_avg_ms = telemetry_frames ? telemetry_encode_sum_ms / telemetry_frames : 0.0;\n        const auto age_in_avg_ms = telemetry_age_samples ? telemetry_age_in_sum_ms / telemetry_age_samples : 0.0;\n        const auto age_out_avg_ms = telemetry_age_samples ? telemetry_age_out_sum_ms / telemetry_age_samples : 0.0;\n\n        BOOST_LOG(info) << \"PIPELINE_TELEMETRY frames=\" << telemetry_frames\n                        << \" fps=\" << (telemetry_frames / elapsed_s)\n                        << \" convert_calls=\" << telemetry_convert_calls\n                        << \" convert_avg_ms=\" << convert_avg_ms\n                        << \" convert_max_ms=\" << telemetry_convert_max_ms\n                        << \" encode_avg_ms=\" << encode_avg_ms\n                        << \" encode_max_ms=\" << telemetry_encode_max_ms\n                        << \" age_samples=\" << telemetry_age_samples\n                        << \" age_in_avg_ms=\" << age_in_avg_ms\n                        << \" age_in_max_ms=\" << telemetry_age_in_max_ms\n                        << \" age_out_avg_ms=\" << age_out_avg_ms\n                        << \" age_out_max_ms=\" << telemetry_age_out_max_ms;\n\n        telemetry_window_start = encode_end;\n        telemetry_frames = telemetry_convert_calls = telemetry_age_samples = 0;\n        telemetry_convert_sum_ms = telemetry_convert_max_ms = 0.0;\n        telemetry_encode_sum_ms = telemetry_encode_max_ms = 0.0;\n        telemetry_age_in_sum_ms = telemetry_age_in_max_ms = 0.0;\n        telemetry_age_out_sum_ms = telemetry_age_out_max_ms = 0.0;\n      }\n\n      session->request_normal_frame();\n"""

for name, anchor in (("anchor1", anchor1), ("anchor2", anchor2), ("anchor3", anchor3)):
    count = s.count(anchor)
    if count != 1:
        raise SystemExit(f"{name}: expected exactly one match, found {count}")

s = s.replace(anchor1, insert1, 1)
s = s.replace(anchor2, insert2, 1)
s = s.replace(anchor3, insert3, 1)

if s.count("PIPELINE_TELEMETRY") != 1:
    raise SystemExit("telemetry marker verification failed")

p.write_text(s)
print("Injected Sunshine video pipeline telemetry into", p)
