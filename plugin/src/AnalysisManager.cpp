#include "AnalysisManager.h"

namespace pitchlane {

AnalysisManager::AnalysisManager() : juce::Thread("PitchLane Analyzer")
{
    formats_.registerBasicFormats();
}

AnalysisManager::~AnalysisManager()
{
    cancelPendingUpdate();
    cancel();
    stopThread(10000);
}

bool AnalysisManager::start(const juce::File& f, const AnalyzerSettings& s)
{
    if (isThreadRunning()) return false;
    file_ = f;
    settings_ = s;
    cancel_ = false;
    progress_ = 0.f;
    status_ = Status::Decoding;
    return startThread(juce::Thread::Priority::low);
}

void AnalysisManager::cancel()
{
    cancel_ = true;
    signalThreadShouldExit();
}

juce::String AnalysisManager::getStatusText() const
{
    switch (status_.load())
    {
        case Status::Idle:      return {};
        case Status::Decoding:  return "Reading audio...";
        case Status::Analyzing: return "Analyzing vocal...";
        case Status::Finished:
        case Status::Failed:
        case Status::Cancelled:
        {
            const juce::ScopedLock sl(resultLock_);
            return result_.message;
        }
    }
    return {};
}

juce::String AnalysisManager::getWildcard() const
{
    return formats_.getWildcardForAllFormats();
}

bool AnalysisManager::canOpen(const juce::File& f) const
{
    return formats_.findFormatForFileExtension(f.getFileExtension()) != nullptr;
}

void AnalysisManager::run()
{
    Result res;
    res.file = file_;
    auto finish = [&](Status st, const juce::String& msg) {
        res.status = st;
        res.message = msg;
        {
            const juce::ScopedLock sl(resultLock_);
            result_ = res;
        }
        status_ = st;
        triggerAsyncUpdate();
    };

    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file_));
    if (reader == nullptr)
    {
        finish(Status::Failed, "Could not open " + file_.getFileName() + " (unsupported or missing file)");
        return;
    }

    const double sr = reader->sampleRate;
    const auto total = reader->lengthInSamples;
    if (sr <= 0 || total <= 0)
    {
        finish(Status::Failed, "The file contains no audio");
        return;
    }
    if (total / sr > kMaxSeconds)
    {
        finish(Status::Failed, "File is longer than 20 minutes");
        return;
    }

    // Decode in chunks, mixing to mono (0 - 15 % of progress).
    std::vector<float> mono(static_cast<size_t>(total), 0.f);
    const int numCh = static_cast<int>(juce::jmax(1u, reader->numChannels));
    juce::AudioBuffer<float> chunk(numCh, 65536);
    for (juce::int64 pos = 0; pos < total; pos += chunk.getNumSamples())
    {
        if (cancel_ || threadShouldExit())
        {
            finish(Status::Cancelled, "Analysis cancelled");
            return;
        }
        const int n = static_cast<int>(juce::jmin<juce::int64>(chunk.getNumSamples(), total - pos));
        reader->read(&chunk, 0, n, pos, true, true);
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* src = chunk.getReadPointer(ch);
            for (int i = 0; i < n; ++i) mono[static_cast<size_t>(pos + i)] += src[i] / static_cast<float>(numCh);
        }
        progress_ = 0.15f * static_cast<float>(pos + n) / static_cast<float>(total);
    }

    status_ = Status::Analyzing;
    auto ar = analyzeMonophonic(mono.data(), mono.size(), sr, settings_, [this](float p) {
        progress_ = 0.15f + 0.85f * p;
        return !(cancel_.load() || threadShouldExit());
    });

    if (ar.cancelled)
    {
        finish(Status::Cancelled, "Analysis cancelled");
        return;
    }
    if (!ar.error.empty())
    {
        finish(Status::Failed, juce::String(ar.error));
        return;
    }
    res.notes = std::move(ar.notes);
    progress_ = 1.f;
    finish(Status::Finished, juce::String(res.notes.size()) + " notes found in " + file_.getFileName());
}

void AnalysisManager::handleAsyncUpdate()
{
    Result r;
    {
        const juce::ScopedLock sl(resultLock_);
        r = result_;
    }
    if (onFinished) onFinished(r);
}

} // namespace pitchlane
