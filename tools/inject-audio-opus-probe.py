#!/usr/bin/env python3
from pathlib import Path

p = Path("source/src/audio.cpp")
s = p.read_text()

include_anchor = '''// standard includes\n#include <thread>\n'''
include_repl = '''// standard includes\n#include <cstdlib>\n#include <fstream>\n#include <thread>\n'''
if s.count(include_anchor) != 1:
    raise SystemExit(f"include anchor count={s.count(include_anchor)}")
s = s.replace(include_anchor, include_repl, 1)

alias_anchor = '''  using namespace std::literals;\n  using opus_t = util::safe_ptr<OpusMSEncoder, opus_multistream_encoder_destroy>;\n  using sample_queue_t = std::shared_ptr<safe::queue_t<std::vector<float>>>;\n'''
alias_repl = '''  using namespace std::literals;\n  using opus_t = util::safe_ptr<OpusMSEncoder, opus_multistream_encoder_destroy>;\n  using opus_decoder_t = util::safe_ptr<OpusMSDecoder, opus_multistream_decoder_destroy>;\n  using sample_queue_t = std::shared_ptr<safe::queue_t<std::vector<float>>>;\n'''
if s.count(alias_anchor) != 1:
    raise SystemExit(f"alias anchor count={s.count(alias_anchor)}")
s = s.replace(alias_anchor, alias_repl, 1)

setup_anchor = '''    auto frame_size = config.packetDuration * stream.sampleRate / 1000;\n    while (auto sample = samples->pop()) {\n      buffer_t packet {1400};\n\n      int bytes = opus_multistream_encode_float(opus.get(), sample->data(), frame_size, std::begin(packet), (opus_int32) packet.size());\n'''
setup_repl = '''    auto frame_size = config.packetDuration * stream.sampleRate / 1000;\n\n    // Diagnostic probe: capture exactly what Sunshine gives Opus and what the\n    // resulting Opus packet decodes to locally. It is opt-in so normal builds\n    // pay no extra I/O or decode cost unless SUNSHINE_AUDIO_PROBE=1 is set.\n    const char *probe_env = std::getenv("SUNSHINE_AUDIO_PROBE");\n    const bool probe_requested = probe_env && std::string_view {probe_env} == "1"sv;\n    constexpr std::int64_t probe_seconds = 45;\n    const std::int64_t probe_target_frames = static_cast<std::int64_t>(stream.sampleRate) * probe_seconds;\n    std::int64_t probe_frames_written = 0;\n    bool probe_active = false;\n    opus_decoder_t probe_decoder;\n    std::ofstream probe_pre;\n    std::ofstream probe_post;\n    std::vector<float> probe_decoded;\n\n    if (probe_requested) {\n      int decoder_error = OPUS_OK;\n      probe_decoder.reset(opus_multistream_decoder_create(\n        stream.sampleRate,\n        stream.channelCount,\n        stream.streams,\n        stream.coupledStreams,\n        stream.mapping,\n        &decoder_error\n      ));\n\n      if (!probe_decoder || decoder_error != OPUS_OK) {\n        BOOST_LOG(error) << "AUDIO_PROBE decoder init failed: "sv << opus_strerror(decoder_error);\n      } else {\n        probe_pre.open("/userdata/system/sunshine-prod/audio-preopus.f32le", std::ios::binary | std::ios::trunc);\n        probe_post.open("/userdata/system/sunshine-prod/audio-postopus.f32le", std::ios::binary | std::ios::trunc);\n        if (!probe_pre || !probe_post) {\n          BOOST_LOG(error) << "AUDIO_PROBE could not open diagnostic output files"sv;\n          probe_pre.close();\n          probe_post.close();\n          probe_decoder.reset();\n        } else {\n          probe_decoded.resize(static_cast<std::size_t>(frame_size) * stream.channelCount);\n          probe_active = true;\n          BOOST_LOG(info) << "AUDIO_PROBE enabled duration="sv << probe_seconds\n                          << "s rate="sv << stream.sampleRate\n                          << " channels="sv << stream.channelCount\n                          << " packet_ms="sv << config.packetDuration\n                          << " frame_size="sv << frame_size;\n        }\n      }\n    }\n\n    while (auto sample = samples->pop()) {\n      buffer_t packet {1400};\n\n      if (probe_active && probe_frames_written < probe_target_frames) {\n        probe_pre.write(\n          reinterpret_cast<const char *>(sample->data()),\n          static_cast<std::streamsize>(sample->size() * sizeof(float))\n        );\n      }\n\n      int bytes = opus_multistream_encode_float(opus.get(), sample->data(), frame_size, std::begin(packet), (opus_int32) packet.size());\n'''
if s.count(setup_anchor) != 1:
    raise SystemExit(f"setup anchor count={s.count(setup_anchor)}")
s = s.replace(setup_anchor, setup_repl, 1)

encode_anchor = '''      if (bytes < 0) {\n        BOOST_LOG(error) << "Couldn't encode audio: "sv << opus_strerror(bytes);\n        packets->stop();\n\n        return;\n      }\n\n      packet.fake_resize(bytes);\n      packets->raise(channel_data, std::move(packet));\n'''
encode_repl = '''      if (bytes < 0) {\n        BOOST_LOG(error) << "Couldn't encode audio: "sv << opus_strerror(bytes);\n        packets->stop();\n\n        return;\n      }\n\n      if (probe_active && probe_frames_written < probe_target_frames) {\n        const int decoded_frames = opus_multistream_decode_float(\n          probe_decoder.get(),\n          std::begin(packet),\n          bytes,\n          probe_decoded.data(),\n          frame_size,\n          0\n        );\n        if (decoded_frames < 0) {\n          BOOST_LOG(error) << "AUDIO_PROBE local Opus decode failed: "sv << opus_strerror(decoded_frames);\n          probe_active = false;\n          probe_pre.close();\n          probe_post.close();\n        } else {\n          probe_post.write(\n            reinterpret_cast<const char *>(probe_decoded.data()),\n            static_cast<std::streamsize>(decoded_frames) * stream.channelCount * sizeof(float)\n          );\n          probe_frames_written += decoded_frames;\n\n          if (probe_frames_written >= probe_target_frames) {\n            probe_pre.flush();\n            probe_post.flush();\n            probe_pre.close();\n            probe_post.close();\n            probe_active = false;\n            BOOST_LOG(info) << "AUDIO_PROBE complete frames_per_channel="sv << probe_frames_written\n                            << " pre=/userdata/system/sunshine-prod/audio-preopus.f32le"sv\n                            << " post=/userdata/system/sunshine-prod/audio-postopus.f32le"sv;\n          }\n        }\n      }\n\n      packet.fake_resize(bytes);\n      packets->raise(channel_data, std::move(packet));\n'''
if s.count(encode_anchor) != 1:
    raise SystemExit(f"encode anchor count={s.count(encode_anchor)}")
s = s.replace(encode_anchor, encode_repl, 1)

if s.count("AUDIO_PROBE enabled") != 1 or s.count("AUDIO_PROBE complete") != 1:
    raise SystemExit("audio probe marker injection failed")

p.write_text(s)
