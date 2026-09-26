#pragma once
// Maps host playhead information (or its absence) to a song position in seconds for each
// audio block, detecting seeks, loop wraps, play/stop and record transitions.
// Real-time safe (no allocation); call update() once per processBlock.

namespace pitchlane {

struct HostPosition
{
    bool valid = false;         // host supplied any position info at all
    bool hasTimeSeconds = false;
    double timeSeconds = 0.0;
    bool hasPpq = false;
    double ppq = 0.0;
    bool hasBpm = false;
    double bpm = 120.0;
    bool isPlaying = false;
    bool isRecording = false;
    bool isLooping = false;
    bool hasLoop = false;
    double loopStartPpq = 0.0, loopEndPpq = 0.0;
    bool hasBarStart = false;
    double barStartPpq = 0.0;   // ppq of the last bar line at or before ppq
};

enum class TimeSource { HostSeconds, HostPpq, FreeRunning };

struct TransportState
{
    double songTime = 0.0;      // seconds at the first sample of the block
    double bpm = 120.0;
    bool playing = false;       // playing or recording
    bool recording = false;
    bool looping = false;
    bool hasLoop = false;
    double loopStart = 0.0, loopEnd = 0.0;   // seconds
    bool jumped = false;        // discontinuity at this block (seek, loop wrap, (re)start)
    bool loopWrapped = false;   // the discontinuity was a jump back to the loop start
    TimeSource source = TimeSource::FreeRunning;
};

class TransportMapper
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    /** Tempo used when the host provides none (and for ppq conversion without bpm). */
    void setManualBpm(double bpm) noexcept { manualBpm_ = bpm > 1.0 ? bpm : 120.0; }

    /** Without host info, run a free clock that is always "playing" (standalone app). */
    void setFreeRunWhenNoHost(bool b) noexcept { freeRun_ = b; }

    /** Restart the free-running clock at 0 (standalone "restart" button). */
    void restartFreeRun() noexcept { freeTime_ = 0.0; freeRestart_ = true; }

    TransportState update(const HostPosition& host, int numSamples) noexcept;

private:
    double sampleRate_ = 44100.0;
    double manualBpm_ = 120.0;
    bool freeRun_ = true;
    double freeTime_ = 0.0;
    bool freeRestart_ = true;

    bool havePrev_ = false;
    bool prevPlaying_ = false;
    double prevTime_ = 0.0;
    double prevBlockSec_ = 0.0;
};

} // namespace pitchlane
