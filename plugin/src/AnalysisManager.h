#pragma once
// Runs "Analyze Vocal" on a background thread: decode the audio file (JUCE formats: WAV,
// AIFF, FLAC, Ogg, MP3; plus M4A/AAC/CAF via Core Audio on macOS), mix to mono, run the
// offline monophonic transcriber, and hand the notes back on the message thread.
// Never touches the audio thread. Progress + cancel are lock-free atomics.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>

#include "pitchlane/OfflineAnalyzer.h"

namespace pitchlane {

/** Decoded audio mixed to mono (all channels averaged), at the file's own sample rate. */
struct DecodedAudio
{
    std::vector<float> mono;
    double sampleRate = 0.0;
    int numChannels = 0;
    juce::String formatName;
    juce::String error;       // empty on success
    bool cancelled = false;
    bool ok() const noexcept { return error.isEmpty() && !cancelled && !mono.empty(); }
};

/** Decodes any file `formats` can read. `progress(fraction)` is called per chunk and returns
    false to cancel. Distinguishes missing files, unsupported types and damaged files in
    `error`. Runs on any non-audio thread. */
DecodedAudio decodeToMono(juce::AudioFormatManager& formats, const juce::File& file, double maxSeconds,
                          const std::function<bool(float)>& progress = {});

/** "WAV, AIFF, FLAC, ..." for messages. */
juce::String supportedFormatsText(juce::AudioFormatManager& formats);

class AnalysisManager : private juce::Thread, private juce::AsyncUpdater
{
public:
    enum class Status { Idle, Decoding, Analyzing, Finished, Failed, Cancelled };

    struct Result
    {
        Status status = Status::Idle;
        NoteList notes;
        juce::String message;
        juce::File file;
    };

    AnalysisManager();
    ~AnalysisManager() override;

    /** Called on the message thread when a job ends (finished, failed or cancelled). */
    std::function<void(const Result&)> onFinished;

    bool start(const juce::File& audioFile, const AnalyzerSettings& settings = {});
    void cancel();
    bool isRunning() const { return isThreadRunning(); }

    float getProgress() const noexcept { return progress_.load(); }
    Status getStatus() const noexcept { return status_.load(); }
    juce::String getStatusText() const;

    /** Formats the analyzer can open (for file choosers / drag and drop). */
    juce::String getWildcard() const;
    bool canOpen(const juce::File& f) const;

    static constexpr double kMaxSeconds = 20.0 * 60.0;

    juce::AudioFormatManager& getFormats() noexcept { return formats_; }

private:
    void run() override;
    void handleAsyncUpdate() override;

    juce::AudioFormatManager formats_;
    juce::File file_;
    AnalyzerSettings settings_;
    std::atomic<float> progress_ { 0.f };
    std::atomic<Status> status_ { Status::Idle };
    std::atomic<bool> cancel_ { false };
    juce::CriticalSection resultLock_;
    Result result_;
};

} // namespace pitchlane
