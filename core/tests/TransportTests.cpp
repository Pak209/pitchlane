#include "TestFramework.h"
#include "pitchlane/Transport.h"

using namespace pitchlane;

namespace {
HostPosition playingAt(double t, double bpm = 120.0)
{
    HostPosition h;
    h.valid = h.hasTimeSeconds = h.hasPpq = h.hasBpm = true;
    h.timeSeconds = t;
    h.ppq = t * bpm / 60.0;
    h.bpm = bpm;
    h.isPlaying = true;
    return h;
}
} // namespace

TEST_CASE("Transport: continuous playback, then seek")
{
    TransportMapper tm;
    tm.prepare(48000.0);
    const int n = 480; // 10 ms blocks
    auto st = tm.update(playingAt(1.0), n);
    CHECK(st.playing);
    CHECK(st.jumped); // start of playback = new pass
    CHECK_NEAR(st.songTime, 1.0, 1e-12);
    CHECK(st.source == TimeSource::HostSeconds);
    double t = 1.0;
    for (int i = 0; i < 100; ++i)
    {
        t += 0.01;
        st = tm.update(playingAt(t), n);
        CHECK(!st.jumped);
    }
    st = tm.update(playingAt(5.0), n); // seek forward
    CHECK(st.jumped);
    CHECK(!st.loopWrapped);
    st = tm.update(playingAt(5.01), n);
    CHECK(!st.jumped);
    st = tm.update(playingAt(0.5), n); // seek backwards
    CHECK(st.jumped);
}

TEST_CASE("Transport: loop wrap is detected")
{
    TransportMapper tm;
    tm.prepare(44100.0);
    const int n = 441;
    // 120 bpm, loop bars 1..2 = ppq 0..8 = 0..4 s
    auto host = [](double t) {
        HostPosition h = playingAt(t);
        h.isLooping = h.hasLoop = true;
        h.loopStartPpq = 0.0;
        h.loopEndPpq = 8.0;
        return h;
    };
    auto st = tm.update(host(3.9), n);
    for (double t = 3.91; t < 3.995; t += 0.01) st = tm.update(host(t), n);
    CHECK(!st.jumped);
    CHECK(st.hasLoop);
    CHECK_NEAR(st.loopStart, 0.0, 1e-9);
    CHECK_NEAR(st.loopEnd, 4.0, 1e-9);
    st = tm.update(host(0.0), n); // wrapped
    CHECK(st.jumped);
    CHECK(st.loopWrapped);
    CHECK(st.looping);
}

TEST_CASE("Transport: stop, record, restart")
{
    TransportMapper tm;
    tm.prepare(48000.0);
    auto st = tm.update(playingAt(2.0), 512);
    HostPosition stopped = playingAt(2.5);
    stopped.isPlaying = false;
    st = tm.update(stopped, 512);
    CHECK(!st.playing);
    CHECK(!st.jumped);
    HostPosition rec = playingAt(2.5);
    rec.isPlaying = false;
    rec.isRecording = true;
    st = tm.update(rec, 512);
    CHECK(st.playing);
    CHECK(st.recording);
    CHECK(st.jumped); // new pass
}

TEST_CASE("Transport: ppq-only host uses bpm; missing host free-runs")
{
    TransportMapper tm;
    tm.prepare(48000.0);
    HostPosition h;
    h.valid = h.hasPpq = h.hasBpm = true;
    h.ppq = 6.0;
    h.bpm = 90.0;
    h.isPlaying = true;
    auto st = tm.update(h, 256);
    CHECK(st.source == TimeSource::HostPpq);
    CHECK_NEAR(st.songTime, 4.0, 1e-12);
    CHECK_NEAR(st.bpm, 90.0, 1e-12);

    // ppq without bpm -> manual tempo
    tm.reset();
    tm.setManualBpm(60.0);
    h.hasBpm = false;
    st = tm.update(h, 256);
    CHECK_NEAR(st.songTime, 6.0, 1e-12);

    // No host info at all -> free-running clock from 0.
    TransportMapper fr;
    fr.prepare(48000.0);
    fr.setManualBpm(100.0);
    HostPosition none;
    st = fr.update(none, 4800);
    CHECK(st.source == TimeSource::FreeRunning);
    CHECK(st.playing);
    CHECK(st.jumped);
    CHECK_NEAR(st.songTime, 0.0, 1e-12);
    CHECK_NEAR(st.bpm, 100.0, 1e-12);
    st = fr.update(none, 4800);
    CHECK(!st.jumped);
    CHECK_NEAR(st.songTime, 0.1, 1e-12);
    fr.restartFreeRun();
    st = fr.update(none, 4800);
    CHECK(st.jumped);
    CHECK_NEAR(st.songTime, 0.0, 1e-12);

    // Free-run disabled -> stopped.
    TransportMapper off;
    off.prepare(48000.0);
    off.setFreeRunWhenNoHost(false);
    st = off.update(none, 256);
    CHECK(!st.playing);
}

TEST_CASE("Transport: small host jitter is not a seek")
{
    TransportMapper tm;
    tm.prepare(48000.0);
    tm.update(playingAt(10.0), 512);
    const double blk = 512 / 48000.0;
    auto st = tm.update(playingAt(10.0 + blk + 0.0005), 512);
    CHECK(!st.jumped);
}
