#include "AnalysisManager.h"

namespace pitchlane {

juce::String supportedFormatsText(juce::AudioFormatManager& formats)
{
    juce::StringArray names;
    for (int i = 0; i < formats.getNumKnownFormats(); ++i)
    {
        auto n = formats.getKnownFormat(i)->getFormatName();
        n = n.upToFirstOccurrenceOf(" file", false, true).trim();   // "WAV file" -> "WAV"
        names.addIfNotAlreadyThere(n);
    }
    return names.joinIntoString(", ");
}

DecodedAudio decodeToMono(juce::AudioFormatManager& formats, const juce::File& file, double maxSeconds,
                          const std::function<bool(float)>& progress)
{
    DecodedAudio out;
    if (!file.existsAsFile())
    {
        out.error = "File not found: " + file.getFileName();
        return out;
    }
    auto* format = formats.findFormatForFileExtension(file.getFileExtension());
    if (format == nullptr)
    {
        out.error = "Unsupported file type \"" + file.getFileExtension() + "\" (" + file.getFileName()
                  + "). Supported here: " + supportedFormatsText(formats) + ".";
        return out;
    }
    out.formatName = format->getFormatName();

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        out.error = "Could not read " + file.getFileName() + ": the file looks damaged or is not a valid "
                  + out.formatName + ".";
        return out;
    }
    const double sr = reader->sampleRate;
    const auto total = reader->lengthInSamples;
    if (sr <= 0 || total <= 0 || reader->numChannels == 0)
    {
        out.error = file.getFileName() + " contains no audio.";
        return out;
    }
    if (static_cast<double>(total) / sr > maxSeconds)
    {
        out.error = file.getFileName() + " is longer than " + juce::String(juce::roundToInt(maxSeconds / 60.0))
                  + " minutes.";
        return out;
    }

    out.sampleRate = sr;
    out.numChannels = static_cast<int>(reader->numChannels);
    out.mono.assign(static_cast<size_t>(total), 0.f);
    const int numCh = out.numChannels;
    const float gain = 1.f / static_cast<float>(numCh);
    juce::AudioBuffer<float> chunk(numCh, 65536);
    for (juce::int64 pos = 0; pos < total; pos += chunk.getNumSamples())
    {
        if (progress && !progress(static_cast<float>(pos) / static_cast<float>(total)))
        {
            out.cancelled = true;
            out.mono.clear();
            return out;
        }
        const int n = static_cast<int>(juce::jmin<juce::int64>(chunk.getNumSamples(), total - pos));
        if (!reader->read(&chunk, 0, n, pos, true, true))
        {
            out.error = "Could not decode " + file.getFileName() + " (read error at "
                      + juce::String(static_cast<double>(pos) / sr, 1) + " s): the file looks damaged.";
            out.mono.clear();
            return out;
        }
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* src = chunk.getReadPointer(ch);
            float* dst = out.mono.data() + pos;
            for (int i = 0; i < n; ++i) dst[i] += src[i] * gain;
        }
    }
    if (progress) progress(1.f);
    return out;
}

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

    auto decoded = decodeToMono(formats_, file_, kMaxSeconds, [this](float p) {
        progress_ = 0.15f * p;   // decoding = 0 - 15 % of progress
        return !(cancel_.load() || threadShouldExit());
    });
    if (decoded.cancelled)
    {
        finish(Status::Cancelled, "Analysis cancelled");
        return;
    }
    if (!decoded.error.isEmpty())
    {
        finish(Status::Failed, decoded.error);
        return;
    }
    const auto& mono = decoded.mono;
    const double sr = decoded.sampleRate;

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
