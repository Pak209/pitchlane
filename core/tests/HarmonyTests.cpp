#include "TestFramework.h"
#include "pitchlane/MidiFile.h"
#include "pitchlane/ReferenceNotes.h"

using namespace pitchlane;

namespace {
// Lead: C5 D5 E5 F5 (quarters at 1 s); harmony a third below, sustained and quieter;
// one bar where the lead rests and only the harmony sings.
NoteList twoVoices()
{
    NoteList n;
    const int lead[] = { 72, 74, 76, 77 };
    const int harm[] = { 69, 71, 72, 74 };
    for (int i = 0; i < 4; ++i)
    {
        n.push_back({ 1.0 * i, 0.95, lead[i], 1.f, 100 });
        n.push_back({ 1.0 * i, 1.0, harm[i], 1.f, 70 });
    }
    n.push_back({ 4.0, 1.0, 67, 1.f, 70 });   // harmony alone: stays in the lead line
    sortNotes(n);
    return n;
}
} // namespace

TEST_CASE("Harmony: polyphony detection and lead-line marking")
{
    auto n = twoVoices();
    CHECK_EQ(maxPolyphony(n), 2);
    NoteList legato { { 0.0, 0.52, 60, 1.f, 100 }, { 0.5, 0.5, 62, 1.f, 100 } };   // 20 ms overlap
    CHECK_EQ(maxPolyphony(legato), 1);

    auto hi = n;
    CHECK_EQ(markLeadLine(hi, LeadMode::Highest), 4);
    for (const auto& x : hi)
    {
        const bool isLead = x.velocity == 100 || x.start >= 4.0;
        CHECK_EQ(x.muted(), !isLead);
        CHECK_EQ(x.harmonySuspect(), !isLead);
    }
    auto only = hi;
    removeMuted(only);
    CHECK_EQ(only.size(), size_t(5));
    CHECK_EQ(maxPolyphony(only), 1);

    // Loudest/most sustained: here the lead is louder, so the same notes win...
    auto loud = n;
    CHECK_EQ(markLeadLine(loud, LeadMode::LoudestSustained), 4);
    for (size_t i = 0; i < n.size(); ++i) CHECK_EQ(loud[i].muted(), hi[i].muted());
    // ...but a quiet high descant over a loud sustained melody picks the melody.
    NoteList descant { { 0.0, 2.0, 60, 1.f, 110 }, { 0.0, 0.5, 79, 1.f, 50 }, { 0.5, 0.5, 81, 1.f, 50 }, { 1.0, 0.5, 83, 1.f, 50 } };
    sortNotes(descant);
    auto d1 = descant, d2 = descant;
    markLeadLine(d1, LeadMode::Highest);
    markLeadLine(d2, LeadMode::LoudestSustained);
    for (const auto& x : d1) CHECK_EQ(x.muted(), x.pitch == 60);
    for (const auto& x : d2) CHECK_EQ(x.muted(), x.pitch != 60);
}

TEST_CASE("Harmony: two-voice MIDI file import, muted notes in export and active-note lookup")
{
    // A two-voice MIDI file (both voices on one track, as many bounced harmonies are).
    const auto bytes = writeMidiFile(twoVoices(), 120.0);
    const auto parsed = parseMidiFile(bytes.data(), bytes.size());
    CHECK(parsed.ok);
    auto notes = referenceNotesFromMidi(parsed);
    CHECK_EQ(notes.size(), size_t(9));
    CHECK_EQ(maxPolyphony(notes), 2);
    CHECK_EQ(markLeadLine(notes, LeadMode::Highest), 4);

    // Active-note lookup skips muted harmony notes (the lead wins even though both started together).
    const int a = findActiveNote(notes, 0.5);
    CHECK(a >= 0);
    if (a >= 0) CHECK_EQ(notes[static_cast<size_t>(a)].pitch, 72);
    // In the lead's release gap (0.95-1.0 s) only the muted harmony sounds -> nothing active.
    CHECK_EQ(findActiveNote(notes, 0.97), -1);

    // Export leaves muted notes out (unless asked).
    const auto out = parseMidiFile(writeMidiFile(notes, 120.0).data(), writeMidiFile(notes, 120.0).size());
    CHECK_EQ(out.notes.size(), size_t(5));
    const auto all = writeMidiFile(notes, 120.0, 480, "x", true);
    CHECK_EQ(parseMidiFile(all.data(), all.size()).notes.size(), size_t(9));
}
