#include "pitchlane/Transport.h"

#include <algorithm>
#include <cmath>

namespace pitchlane {

void TransportMapper::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0 ? sampleRate : 44100.0;
    reset();
}

void TransportMapper::reset() noexcept
{
    havePrev_ = prevPlaying_ = false;
    prevTime_ = prevBlockSec_ = 0.0;
    freeRestart_ = true;
}

TransportState TransportMapper::update(const HostPosition& host, int numSamples) noexcept
{
    TransportState st;
    const double blockSec = numSamples / sampleRate_;
    st.bpm = (host.valid && host.hasBpm && host.bpm > 1.0) ? host.bpm : manualBpm_;

    const bool hostTime = host.valid && (host.hasTimeSeconds || host.hasPpq);
    if (hostTime)
    {
        st.source = host.hasTimeSeconds ? TimeSource::HostSeconds : TimeSource::HostPpq;
        st.songTime = host.hasTimeSeconds ? host.timeSeconds : host.ppq * 60.0 / st.bpm;
        st.playing = host.isPlaying || host.isRecording;
        st.recording = host.isRecording;
        st.looping = host.isLooping;
        if (host.hasLoop && host.loopEndPpq > host.loopStartPpq)
        {
            st.hasLoop = true;
            st.loopStart = host.loopStartPpq * 60.0 / st.bpm;
            st.loopEnd = host.loopEndPpq * 60.0 / st.bpm;
        }
    }
    else
    {
        st.source = TimeSource::FreeRunning;
        st.playing = freeRun_ || (host.valid && host.isPlaying);
        st.recording = host.valid && host.isRecording;
        st.songTime = freeTime_;
        if (freeRestart_) { st.jumped = true; freeRestart_ = false; }
        if (st.playing) freeTime_ += blockSec;
    }

    if (st.playing)
    {
        if (!havePrev_ || !prevPlaying_)
        {
            st.jumped = true; // playback (re)started: a new pass begins here
        }
        else
        {
            const double expected = prevTime_ + prevBlockSec_;
            // Tolerate host jitter / rounding (a couple of ms, or a fraction of the block).
            const double tol = std::max(0.004, 0.5 * prevBlockSec_);
            if (std::abs(st.songTime - expected) > tol)
            {
                st.jumped = true;
                if (st.hasLoop && st.songTime < expected
                    && std::abs(st.songTime - st.loopStart) < std::max(0.05, 2.0 * blockSec + tol)
                    && expected >= st.loopEnd - std::max(0.05, 2.0 * blockSec + tol))
                    st.loopWrapped = true;
            }
        }
    }

    havePrev_ = true;
    prevPlaying_ = st.playing;
    prevTime_ = st.songTime;
    prevBlockSec_ = blockSec;
    return st;
}

} // namespace pitchlane
