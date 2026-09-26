#include "pitchlane/MidiFile.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace pitchlane {

namespace {

struct Reader
{
    const uint8_t* p;
    const uint8_t* end;
    bool failed = false;

    bool has(size_t n) const { return static_cast<size_t>(end - p) >= n; }
    uint8_t u8()
    {
        if (!has(1)) { failed = true; return 0; }
        return *p++;
    }
    uint32_t be(int bytes)
    {
        uint32_t v = 0;
        for (int i = 0; i < bytes; ++i) v = (v << 8) | u8();
        return v;
    }
    uint32_t vlq()
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            const uint8_t b = u8();
            v = (v << 7) | (b & 0x7Fu);
            if (!(b & 0x80u)) return v;
        }
        failed = true;
        return v;
    }
    void skip(size_t n)
    {
        if (!has(n)) { failed = true; p = end; return; }
        p += n;
    }
};

struct TempoPoint
{
    uint64_t tick;
    double usPerQuarter;
};

struct RawNote
{
    uint64_t startTick, endTick;
    int pitch, velocity, channel, track;
};

class TickConverter
{
public:
    TickConverter(std::vector<TempoPoint> tempos, int tpq, double smpteTicksPerSecond)
        : tpq_(tpq), smpte_(smpteTicksPerSecond)
    {
        std::stable_sort(tempos.begin(), tempos.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
        if (tempos.empty() || tempos.front().tick != 0) tempos.insert(tempos.begin(), { 0, 500000.0 });
        double sec = 0.0;
        for (size_t i = 0; i < tempos.size(); ++i)
        {
            if (i > 0)
                sec += static_cast<double>(tempos[i].tick - tempos[i - 1].tick) * tempos[i - 1].usPerQuarter * 1e-6 / tpq_;
            points_.push_back({ tempos[i].tick, tempos[i].usPerQuarter, sec });
        }
    }

    double seconds(uint64_t tick) const
    {
        if (smpte_ > 0.0) return static_cast<double>(tick) / smpte_;
        auto it = std::upper_bound(points_.begin(), points_.end(), tick,
                                   [](uint64_t t, const Pt& p) { return t < p.tick; });
        --it;
        return it->sec + static_cast<double>(tick - it->tick) * it->us * 1e-6 / tpq_;
    }

private:
    struct Pt { uint64_t tick; double us; double sec; };
    std::vector<Pt> points_;
    int tpq_;
    double smpte_;
};

void putBE(std::vector<uint8_t>& o, uint32_t v, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i) o.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

void putVlq(std::vector<uint8_t>& o, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = static_cast<uint8_t>(v & 0x7F);
    while ((v >>= 7) != 0) buf[n++] = static_cast<uint8_t>((v & 0x7F) | 0x80);
    while (n > 0) o.push_back(buf[--n]);
}

} // namespace

MidiParseResult parseMidiFile(const uint8_t* data, size_t size)
{
    MidiParseResult res;
    Reader r { data, data + size };

    if (size < 14 || r.be(4) != 0x4D546864u /* MThd */)
    {
        res.error = "Not a Standard MIDI File";
        return res;
    }
    const uint32_t hdrLen = r.be(4);
    res.format = static_cast<int>(r.be(2));
    const int numTracks = static_cast<int>(r.be(2));
    const uint16_t division = static_cast<uint16_t>(r.be(2));
    if (hdrLen > 6) r.skip(hdrLen - 6);
    if (r.failed || res.format > 2) { res.error = "Bad MIDI header"; return res; }

    double smpteTps = 0.0;
    int tpq = 480;
    if (division & 0x8000u)
    {
        const int fps = -static_cast<int8_t>(division >> 8);
        const int tpf = division & 0xFF;
        smpteTps = (fps == 29 ? 29.97 : fps) * tpf;
        res.ticksPerQuarter = 0;
    }
    else
    {
        tpq = std::max<int>(1, division);
        res.ticksPerQuarter = tpq;
    }

    std::vector<TempoPoint> tempos;
    std::vector<RawNote> raw;

    for (int t = 0; t < numTracks && r.has(8); ++t)
    {
        const uint32_t id = r.be(4);
        const uint32_t len = r.be(4);
        if (!r.has(len)) { res.error = "Truncated MIDI track"; return res; }
        if (id != 0x4D54726Bu /* MTrk */) { r.skip(len); continue; }

        Reader tr { r.p, r.p + len };
        r.skip(len);

        MidiTrackInfo info;
        uint64_t tick = 0;
        uint8_t status = 0;
        std::map<int, std::vector<std::pair<uint64_t, int>>> open; // key (ch*128+pitch) -> starts

        while (tr.has(1) && !tr.failed)
        {
            tick += tr.vlq();
            uint8_t b = tr.u8();
            if (b == 0xFF)
            {
                const uint8_t type = tr.u8();
                const uint32_t mlen = tr.vlq();
                if (!tr.has(mlen)) break;
                if (type == 0x51 && mlen == 3)
                {
                    const double us = (tr.p[0] << 16) | (tr.p[1] << 8) | tr.p[2];
                    if (us > 0) tempos.push_back({ tick, us });
                }
                else if (type == 0x03 && info.name.empty())
                {
                    info.name.assign(reinterpret_cast<const char*>(tr.p), mlen);
                }
                tr.skip(mlen);
                if (type == 0x2F) break;
                continue;
            }
            if (b == 0xF0 || b == 0xF7)
            {
                tr.skip(tr.vlq());
                continue;
            }

            uint8_t d1;
            if (b & 0x80u)
            {
                status = b;
                d1 = tr.u8();
            }
            else
            {
                if (status == 0) { res.error = "Running status without status byte"; return res; }
                d1 = b;
            }
            const int type = status & 0xF0, ch = status & 0x0F;
            const bool twoBytes = !(type == 0xC0 || type == 0xD0);
            const uint8_t d2 = twoBytes ? tr.u8() : 0;

            const int key = ch * 128 + (d1 & 0x7F);
            if (type == 0x90 && d2 > 0)
            {
                open[key].push_back({ tick, d2 });
            }
            else if (type == 0x80 || (type == 0x90 && d2 == 0))
            {
                auto it = open.find(key);
                if (it != open.end() && !it->second.empty())
                {
                    const auto st = it->second.front(); // FIFO pairing
                    it->second.erase(it->second.begin());
                    raw.push_back({ st.first, tick, d1 & 0x7F, st.second, ch, t });
                    ++info.noteCount;
                }
            }
        }
        // Close hanging notes at the end of the track.
        for (auto& kv : open)
            for (auto& st : kv.second)
            {
                raw.push_back({ st.first, std::max(tick, st.first + 1), kv.first % 128, st.second, kv.first / 128, t });
                ++info.noteCount;
            }
        if (tr.failed) { res.error = "Corrupt MIDI track data"; return res; }
        res.tracks.push_back(info);
    }

    TickConverter conv(tempos, tpq, smpteTps);
    if (!tempos.empty())
    {
        auto first = std::min_element(tempos.begin(), tempos.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
        res.initialBpm = 60e6 / first->usPerQuarter;
    }
    for (const auto& n : raw)
    {
        MidiNoteEvent e;
        e.start = conv.seconds(n.startTick);
        e.length = std::max(0.0, conv.seconds(n.endTick) - e.start);
        e.pitch = n.pitch;
        e.velocity = n.velocity;
        e.channel = n.channel;
        e.track = n.track;
        res.notes.push_back(e);
    }
    std::stable_sort(res.notes.begin(), res.notes.end(), [](auto& a, auto& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    res.ok = true;
    return res;
}

NoteList referenceNotesFromMidi(const MidiParseResult& midi, int trackIndex)
{
    if (trackIndex < 0)
    {
        std::map<int, int> counts;
        for (const auto& n : midi.notes)
            if (n.channel != 9) ++counts[n.track];
        int best = -1, bestCount = 0;
        for (auto& kv : counts)
            if (kv.second > bestCount) { best = kv.first; bestCount = kv.second; }
        trackIndex = best;
    }
    NoteList out;
    for (const auto& n : midi.notes)
    {
        if (n.track != trackIndex || n.channel == 9) continue;
        RefNote r;
        r.start = n.start;
        r.length = n.length;
        r.pitch = n.pitch;
        r.velocity = n.velocity;
        r.confidence = 1.f;
        out.push_back(r);
    }
    sortNotes(out);
    sanitiseNotes(out);
    return out;
}

std::vector<uint8_t> writeMidiFile(const NoteList& notes, double bpm, int tpq, const std::string& trackName)
{
    bpm = (bpm > 1.0 && bpm < 1000.0) ? bpm : 120.0;
    tpq = std::clamp(tpq, 24, 32767);
    const double ticksPerSec = tpq * bpm / 60.0;

    std::vector<uint8_t> out;
    auto chunk = [&](const char* id, const std::vector<uint8_t>& body) {
        out.insert(out.end(), id, id + 4);
        putBE(out, static_cast<uint32_t>(body.size()), 4);
        out.insert(out.end(), body.begin(), body.end());
    };

    std::vector<uint8_t> hdr;
    putBE(hdr, 1, 2);
    putBE(hdr, 2, 2);
    putBE(hdr, static_cast<uint32_t>(tpq), 2);
    chunk("MThd", hdr);

    std::vector<uint8_t> t0;
    putVlq(t0, 0);
    const uint32_t us = static_cast<uint32_t>(std::lround(60e6 / bpm));
    t0.insert(t0.end(), { 0xFF, 0x51, 0x03 });
    putBE(t0, us, 3);
    putVlq(t0, 0);
    t0.insert(t0.end(), { 0xFF, 0x2F, 0x00 });
    chunk("MTrk", t0);

    struct Ev { uint64_t tick; int order; uint8_t s, d1, d2; };
    std::vector<Ev> evs;
    for (const auto& n : notes)
    {
        const auto a = static_cast<uint64_t>(std::llround(std::max(0.0, n.start) * ticksPerSec));
        auto b = static_cast<uint64_t>(std::llround(std::max(0.0, n.end()) * ticksPerSec));
        if (b <= a) b = a + 1;
        const auto p = static_cast<uint8_t>(std::clamp(n.pitch, 0, 127));
        const auto v = static_cast<uint8_t>(std::clamp(n.velocity, 1, 127));
        evs.push_back({ a, 1, 0x90, p, v });
        evs.push_back({ b, 0, 0x80, p, 0 }); // note-offs first at equal ticks
    }
    std::stable_sort(evs.begin(), evs.end(), [](auto& x, auto& y) {
        return x.tick != y.tick ? x.tick < y.tick : x.order < y.order;
    });

    std::vector<uint8_t> t1;
    putVlq(t1, 0);
    t1.insert(t1.end(), { 0xFF, 0x03 });
    putVlq(t1, static_cast<uint32_t>(trackName.size()));
    t1.insert(t1.end(), trackName.begin(), trackName.end());
    uint64_t last = 0;
    for (const auto& e : evs)
    {
        putVlq(t1, static_cast<uint32_t>(e.tick - last));
        last = e.tick;
        t1.insert(t1.end(), { e.s, e.d1, e.d2 });
    }
    putVlq(t1, 0);
    t1.insert(t1.end(), { 0xFF, 0x2F, 0x00 });
    chunk("MTrk", t1);
    return out;
}

} // namespace pitchlane
