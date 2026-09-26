#include <vector>

#include "TestFramework.h"
#include "pitchlane/MidiFile.h"

using namespace pitchlane;

TEST_CASE("MIDI: write -> parse round trip")
{
    NoteList notes = {
        { 0.0, 0.5, 60, 1.f, 100 },
        { 0.5, 0.25, 62, 1.f, 90 },
        { 1.0, 1.0, 64, 1.f, 80 },
        { 2.5, 0.125, 67, 1.f, 127 },
        { 2.5, 0.5, 72, 1.f, 64 }, // overlapping chord note
    };
    for (double bpm : { 120.0, 93.0, 174.0 })
    {
        const auto bytes = writeMidiFile(notes, bpm);
        const auto parsed = parseMidiFile(bytes.data(), bytes.size());
        CHECK(parsed.ok);
        CHECK_EQ(parsed.format, 1);
        CHECK_EQ(parsed.ticksPerQuarter, 480);
        CHECK_NEAR(parsed.initialBpm, bpm, 0.01);
        CHECK_EQ(parsed.tracks.size(), size_t(2));
        const auto back = referenceNotesFromMidi(parsed);
        CHECK_EQ(back.size(), notes.size());
        const double tickSec = 60.0 / bpm / 480.0;
        for (size_t i = 0; i < std::min(back.size(), notes.size()); ++i)
        {
            CHECK_EQ(back[i].pitch, notes[i].pitch);
            CHECK_EQ(back[i].velocity, notes[i].velocity);
            CHECK_NEAR(back[i].start, notes[i].start, tickSec);
            CHECK_NEAR(back[i].length, notes[i].length, 2 * tickSec);
        }
    }
}

namespace {
void vlq(std::vector<uint8_t>& o, uint32_t v)
{
    uint8_t b[5]; int n = 0;
    b[n++] = v & 0x7F;
    while ((v >>= 7)) b[n++] = static_cast<uint8_t>((v & 0x7F) | 0x80);
    while (n) o.push_back(b[--n]);
}
void be(std::vector<uint8_t>& o, uint32_t v, int bytes) { for (int i = bytes - 1; i >= 0; --i) o.push_back((v >> (8 * i)) & 0xFF); }
} // namespace

TEST_CASE("MIDI: hand-built file with running status, tempo change, vel-0 note-offs, drums")
{
    // Format 1, 96 tpq. Track 0: tempo 120 then 60 at tick 192 (beat 2). Track 1: melody.
    std::vector<uint8_t> f = { 'M', 'T', 'h', 'd' };
    be(f, 6, 4); be(f, 1, 2); be(f, 3, 2); be(f, 96, 2);

    std::vector<uint8_t> t0;
    vlq(t0, 0); t0.insert(t0.end(), { 0xFF, 0x51, 0x03 }); be(t0, 500000, 3);
    vlq(t0, 192); t0.insert(t0.end(), { 0xFF, 0x51, 0x03 }); be(t0, 1000000, 3);
    vlq(t0, 0); t0.insert(t0.end(), { 0xFF, 0x2F, 0x00 });
    f.insert(f.end(), { 'M', 'T', 'r', 'k' }); be(f, static_cast<uint32_t>(t0.size()), 4); f.insert(f.end(), t0.begin(), t0.end());

    std::vector<uint8_t> t1;
    vlq(t1, 0); t1.insert(t1.end(), { 0xFF, 0x03, 0x04, 'L', 'e', 'a', 'd' });
    vlq(t1, 0); t1.insert(t1.end(), { 0x90, 60, 100 });  // C4 on at 0
    vlq(t1, 96); t1.insert(t1.end(), { 60, 0 });          // running status, vel 0 = off at tick 96 (0.5 s)
    vlq(t1, 0); t1.insert(t1.end(), { 64, 90 });          // E4 on at 96
    vlq(t1, 0); t1.insert(t1.end(), { 0xF0, 0x02, 0x01, 0xF7 }); // sysex in the middle
    vlq(t1, 192); t1.insert(t1.end(), { 0x80, 64, 0 });   // off at 288: 192->1.0 s, +96 @60bpm = 2.0 s
    vlq(t1, 0); t1.insert(t1.end(), { 0xFF, 0x2F, 0x00 });
    f.insert(f.end(), { 'M', 'T', 'r', 'k' }); be(f, static_cast<uint32_t>(t1.size()), 4); f.insert(f.end(), t1.begin(), t1.end());

    std::vector<uint8_t> t2; // drums, more notes than the melody but channel 10
    for (int i = 0; i < 4; ++i) { vlq(t2, 0); t2.insert(t2.end(), { 0x99, 36, 100 }); vlq(t2, 24); t2.insert(t2.end(), { 0x89, 36, 0 }); }
    vlq(t2, 0); t2.insert(t2.end(), { 0xFF, 0x2F, 0x00 });
    f.insert(f.end(), { 'M', 'T', 'r', 'k' }); be(f, static_cast<uint32_t>(t2.size()), 4); f.insert(f.end(), t2.begin(), t2.end());

    const auto res = parseMidiFile(f.data(), f.size());
    CHECK(res.ok);
    CHECK_EQ(res.tracks.size(), size_t(3));
    CHECK_EQ(res.tracks[1].name, std::string("Lead"));
    CHECK_NEAR(res.initialBpm, 120.0, 1e-9);
    const auto notes = referenceNotesFromMidi(res);
    CHECK_EQ(notes.size(), size_t(2));
    if (notes.size() == 2)
    {
        CHECK_EQ(notes[0].pitch, 60);
        CHECK_NEAR(notes[0].start, 0.0, 1e-9);
        CHECK_NEAR(notes[0].length, 0.5, 1e-9);
        CHECK_EQ(notes[1].pitch, 64);
        CHECK_NEAR(notes[1].start, 0.5, 1e-9);
        CHECK_NEAR(notes[1].end(), 2.0, 1e-9);
    }
}

TEST_CASE("MIDI: garbage and truncated input fail gracefully")
{
    const uint8_t junk[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    CHECK(!parseMidiFile(junk, sizeof(junk)).ok);
    auto good = writeMidiFile({ { 0.0, 1.0, 60, 1.f, 100 } }, 120.0);
    good.resize(good.size() - 6);
    const auto res = parseMidiFile(good.data(), good.size());
    CHECK(!res.ok);
    CHECK(!res.error.empty());
    CHECK(!parseMidiFile(nullptr, 0).ok);
}
