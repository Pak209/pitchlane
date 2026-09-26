#pragma once
// Minimal RIFF/WAVE reader for the command-line tools (PCM 16/24/32-bit, IEEE float 32/64,
// any channel count, mixed to mono). Not used by the plugin (which decodes with JUCE).

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace pitchlane::tools {

struct MonoAudio
{
    std::vector<float> samples;
    double sampleRate = 0.0;
    int channels = 0;
    std::string error;
};

inline MonoAudio readWavMono(const std::string& path)
{
    MonoAudio out;
    std::ifstream f(path, std::ios::binary);
    if (!f) { out.error = "cannot open " + path; return out; }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t p) { return static_cast<uint32_t>(d[p] | (d[p + 1] << 8)); };
    auto u32 = [&](size_t p) { return static_cast<uint32_t>(d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | (static_cast<uint32_t>(d[p + 3]) << 24)); };
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
    { out.error = "not a RIFF/WAVE file"; return out; }
    int fmt = 0, ch = 0, bits = 0; uint32_t sr = 0;
    size_t dataPos = 0, dataLen = 0;
    for (size_t p = 12; p + 8 <= d.size();)
    {
        const uint32_t len = u32(p + 4);
        if (std::memcmp(d.data() + p, "fmt ", 4) == 0 && p + 8 + 16 <= d.size())
        {
            fmt = static_cast<int>(u16(p + 8)); ch = static_cast<int>(u16(p + 10)); sr = u32(p + 12); bits = static_cast<int>(u16(p + 22));
            if (fmt == 0xFFFE && len >= 40) fmt = static_cast<int>(u16(p + 8 + 24)); // WAVE_FORMAT_EXTENSIBLE sub-format
        }
        else if (std::memcmp(d.data() + p, "data", 4) == 0)
        {
            dataPos = p + 8;
            dataLen = std::min<size_t>(len, d.size() - dataPos);
        }
        p += 8 + len + (len & 1u);
    }
    if (dataPos == 0 || ch <= 0 || sr == 0) { out.error = "missing fmt/data chunk"; return out; }
    const int bps = bits / 8;
    if (!((fmt == 1 && (bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && (bits == 32 || bits == 64))))
    { out.error = "unsupported WAV encoding"; return out; }
    const size_t frames = dataLen / static_cast<size_t>(bps * ch);
    out.samples.assign(frames, 0.f);
    out.sampleRate = sr;
    out.channels = ch;
    for (size_t i = 0; i < frames; ++i)
    {
        double acc = 0.0;
        for (int c = 0; c < ch; ++c)
        {
            const uint8_t* s = d.data() + dataPos + (i * static_cast<size_t>(ch) + static_cast<size_t>(c)) * static_cast<size_t>(bps);
            double v = 0.0;
            if (fmt == 3 && bits == 32) { float x; std::memcpy(&x, s, 4); v = x; }
            else if (fmt == 3) { double x; std::memcpy(&x, s, 8); v = x; }
            else if (bits == 16) v = static_cast<int16_t>(s[0] | (s[1] << 8)) / 32768.0;
            else if (bits == 24) { int32_t x = (s[0] << 8) | (s[1] << 16) | (s[2] << 24); v = (x >> 8) / 8388608.0; }
            else { int32_t x; std::memcpy(&x, s, 4); v = x / 2147483648.0; }
            acc += v;
        }
        out.samples[i] = static_cast<float>(acc / ch);
    }
    return out;
}

} // namespace pitchlane::tools
