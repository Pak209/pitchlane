#include "TestFramework.h"
#include "pitchlane/ReferenceNotes.h"

using namespace pitchlane;

TEST_CASE("ReferenceNotes: active note lookup, mapping, merge, short removal")
{
    NoteList n = { { 2.0, 1.0, 64 }, { 0.0, 1.0, 60 }, { 1.0, 0.5, 62 }, { 3.5, 0.5, 67 } };
    sortNotes(n);
    CHECK_EQ(n[0].pitch, 60);
    CHECK_EQ(findActiveNote(n, 0.5), 0);
    CHECK_EQ(findActiveNote(n, 1.2), 1);
    CHECK_EQ(findActiveNote(n, 1.7), -1);
    CHECK_EQ(findActiveNote(n, 2.99), 2);
    CHECK_EQ(findActiveNote(n, 3.0), -1);
    CHECK_EQ(findActiveNote(n, -1.0), -1);
    CHECK_EQ(firstNoteEndingAfter(n, 1.6), size_t(2));
    CHECK_EQ(firstNoteEndingAfter(n, 10.0), n.size());

    ReferenceMapping m { 0.25, -2 };
    CHECK_NEAR(m.toSong(1.0), 1.25, 1e-12);
    CHECK_NEAR(m.toRef(1.25), 1.0, 1e-12);
    CHECK_EQ(m.targetPitch(n[0]), 58);

    NoteList g = { { 0.0, 0.5, 60 }, { 0.55, 0.5, 60 }, { 1.2, 0.5, 60 }, { 1.7, 0.02, 62 } };
    mergeSamePitchGaps(g, 0.06);
    CHECK_EQ(g.size(), size_t(3));
    CHECK_NEAR(g[0].length, 1.05, 1e-12);
    removeShortNotes(g, 0.05);
    CHECK_EQ(g.size(), size_t(2));
}
