#pragma once
// Minimal RIFF/WAVE reader for the voice cues: accepts only what the G1 audio
// service plays (PCM, mono, 16 kHz, 16-bit) and returns the raw sample bytes.
// Header-only, no SDK dependency, so it is probe-testable on its own.
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace kist {

constexpr int kVoiceSampleRate = 16000;
constexpr int kVoiceBytesPerSecond = kVoiceSampleRate * 2;   // mono, 16-bit

// Parses `bytes` as a WAV file. On success fills `pcm` and returns true;
// otherwise returns false and describes the problem in `err`.
inline bool parse_wav16k_mono(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& pcm, std::string& err) {
    auto u16 = [&](size_t o) { return uint16_t(bytes[o] | (bytes[o + 1] << 8)); };
    auto u32 = [&](size_t o) { return uint32_t(u16(o) | (uint32_t(u16(o + 2)) << 16)); };
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4)) {
        err = "not a RIFF/WAVE file";
        return false;
    }
    bool have_fmt = false;
    size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const uint32_t size = u32(pos + 4);
        const size_t body = pos + 8;
        if (body + size > bytes.size()) { err = "truncated chunk"; return false; }
        if (!std::memcmp(bytes.data() + pos, "fmt ", 4)) {
            if (size < 16) { err = "fmt chunk too short"; return false; }
            const uint16_t format = u16(body), channels = u16(body + 2), bits = u16(body + 14);
            const uint32_t rate = u32(body + 4);
            if (format != 1 || channels != 1 || rate != kVoiceSampleRate || bits != 16) {
                err = "need PCM mono 16 kHz 16-bit, got format " + std::to_string(format) + " ch " +
                      std::to_string(channels) + " rate " + std::to_string(rate) + " bits " + std::to_string(bits);
                return false;
            }
            have_fmt = true;
        } else if (!std::memcmp(bytes.data() + pos, "data", 4)) {
            if (!have_fmt) { err = "data chunk before fmt"; return false; }
            pcm.assign(bytes.begin() + body, bytes.begin() + body + size);
            if (pcm.size() % 2) pcm.pop_back();
            if (pcm.empty()) { err = "empty data chunk"; return false; }
            return true;
        }
        pos = body + size + (size & 1);   // chunks are word-aligned
    }
    err = "no data chunk";
    return false;
}

// Scales 16-bit little-endian samples in place; gain is clamped to [0, 1].
inline void scale_pcm16(std::vector<uint8_t>& pcm, double gain) {
    if (gain < 0) gain = 0;
    if (gain > 1) gain = 1;
    if (gain == 1) return;
    for (size_t i = 0; i + 1 < pcm.size(); i += 2) {
        const int16_t s = int16_t(pcm[i] | (pcm[i + 1] << 8));
        const int16_t o = int16_t(s * gain);
        pcm[i] = uint8_t(o & 0xff);
        pcm[i + 1] = uint8_t((o >> 8) & 0xff);
    }
}

inline bool load_wav16k_mono(const std::string& path, std::vector<uint8_t>& pcm, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "cannot open"; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse_wav16k_mono(bytes, pcm, err);
}

}  // namespace kist
