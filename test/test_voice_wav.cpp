// Probe for the voice-cue WAV reader: the shipped cues parse, wrong formats
// are rejected. Run from the repo root (reads assets/voice/*.wav).
#include "system/voice_wav.hpp"

#include <cstdio>
#include <cstdlib>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static std::vector<uint8_t> make_wav(uint16_t fmt, uint16_t ch, uint32_t rate, uint16_t bits, uint32_t data_bytes) {
    std::vector<uint8_t> b;
    auto p16 = [&](uint16_t v) { b.push_back(v & 0xff); b.push_back(v >> 8); };
    auto p32 = [&](uint32_t v) { p16(v & 0xffff); p16(v >> 16); };
    auto tag = [&](const char* t) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(t[i])); };
    tag("RIFF"); p32(36 + data_bytes); tag("WAVE");
    tag("fmt "); p32(16); p16(fmt); p16(ch); p32(rate); p32(rate * ch * bits / 8); p16(ch * bits / 8); p16(bits);
    tag("data"); p32(data_bytes);
    for (uint32_t i = 0; i < data_bytes; ++i) b.push_back(uint8_t(i));
    return b;
}

int main() {
    std::vector<uint8_t> pcm;
    std::string err;

    CHECK(kist::parse_wav16k_mono(make_wav(1, 1, 16000, 16, 320), pcm, err));
    CHECK(pcm.size() == 320);
    CHECK(!kist::parse_wav16k_mono(make_wav(1, 2, 16000, 16, 320), pcm, err));   // stereo
    CHECK(!kist::parse_wav16k_mono(make_wav(1, 1, 24000, 16, 320), pcm, err));   // wrong rate
    CHECK(!kist::parse_wav16k_mono(make_wav(1, 1, 16000, 8, 320), pcm, err));    // 8-bit
    CHECK(!kist::parse_wav16k_mono(make_wav(3, 1, 16000, 16, 320), pcm, err));   // float
    CHECK(!kist::parse_wav16k_mono(make_wav(1, 1, 16000, 16, 0), pcm, err));     // empty
    auto trunc = make_wav(1, 1, 16000, 16, 320); trunc.resize(100);
    CHECK(!kist::parse_wav16k_mono(trunc, pcm, err));
    CHECK(!kist::parse_wav16k_mono({'R','I','F','X'}, pcm, err));

    std::vector<uint8_t> s = {0x10, 0x00, 0xF0, 0xFF, 0x00, 0x80};   // +16, -16, -32768
    kist::scale_pcm16(s, 0.5);
    CHECK(s[0] == 0x08 && s[1] == 0x00);
    CHECK(s[2] == 0xF8 && s[3] == 0xFF);
    CHECK(s[4] == 0x00 && s[5] == 0xC0);                             // -16384
    kist::scale_pcm16(s, 0.0);
    CHECK(s[0] == 0 && s[1] == 0 && s[4] == 0 && s[5] == 0);

    const char* cues[] = {"start", "stop", "success", "fail", "partial"};
    for (const char* c : cues) {
        const std::string path = std::string("assets/voice/") + c + ".wav";
        const bool ok = kist::load_wav16k_mono(path, pcm, err);
        CHECK(ok);
        if (ok) std::printf("%-8s %.2f s\n", c, double(pcm.size()) / kist::kVoiceBytesPerSecond);
        else    std::printf("%-8s %s: %s\n", c, path.c_str(), err.c_str());
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
