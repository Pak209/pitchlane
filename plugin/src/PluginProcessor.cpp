#include "PluginProcessor.h"

#include "Params.h"
#include "PluginEditor.h"
#include "StateCodec.h"

namespace pitchlane {

PitchLaneProcessor::PitchLaneProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "Params", params::createLayout([this] { return getOctaveConvention(); }))
{
    gateParam_ = apvts_.getRawParameterValue(params::gateDb);
    clarityParam_ = apvts_.getRawParameterValue(params::clarity);
    tempoParam_ = apvts_.getRawParameterValue(params::tempo);
    noteNamesParam_ = apvts_.getRawParameterValue(params::noteNames);

    analysis_.onFinished = [this](const AnalysisManager::Result& r) {
        if (r.status == AnalysisManager::Status::Finished)
        {
            reference_.setNotes(r.notes, true); // undoable
            reference_.setSourcePath(r.file.getFullPathName());
        }
    };
}

PitchLaneProcessor::~PitchLaneProcessor()
{
    analysis_.onFinished = nullptr;
}

int PitchLaneProcessor::getOctaveConvention() const noexcept
{
    return noteNamesParam_ != nullptr ? juce::roundToInt(noteNamesParam_->load()) : 0;
}

bool PitchLaneProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    // Mono -> mono and stereo -> stereo only: audio is passed through untouched.
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void PitchLaneProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate > 0 ? sampleRate : 44100.0;
    maxBlock_ = juce::jmax(1, samplesPerBlock);

    PitchDetectorSettings ds;
    ds.gateDb = gateParam_->load();
    ds.maxAperiodicity = 1.f - clarityParam_->load();
    detector_.prepare(sampleRate_, ds);
    smoother_.configure(detector_.hopSeconds());
    transportMapper_.prepare(sampleRate_);
    transportMapper_.setManualBpm(tempoParam_->load());
    transportMapper_.setFreeRunWhenNoHost(true);

    mono_.assign(static_cast<size_t>(maxBlock_), 0.f);
    detectorFrames_.assign(static_cast<size_t>(detector_.maxFramesForBlock(maxBlock_)), {});
    samplesSinceJump_ = 0;
    setLatencySamples(0); // pure pass-through: no added latency
}

void PitchLaneProcessor::releaseResources() {}

HostPosition PitchLaneProcessor::readHost(juce::AudioPlayHead* ph)
{
    HostPosition h;
    if (ph == nullptr) return h;
    const auto pos = ph->getPosition();
    if (!pos.hasValue()) return h;
    h.valid = true;
    if (auto t = pos->getTimeInSeconds()) { h.hasTimeSeconds = true; h.timeSeconds = *t; }
    if (auto p = pos->getPpqPosition()) { h.hasPpq = true; h.ppq = *p; }
    if (auto b = pos->getBpm()) { h.hasBpm = *b > 0.0; h.bpm = *b; }
    h.isPlaying = pos->getIsPlaying();
    h.isRecording = pos->getIsRecording();
    h.isLooping = pos->getIsLooping();
    if (auto lp = pos->getLoopPoints()) { h.hasLoop = true; h.loopStartPpq = lp->ppqStart; h.loopEndPpq = lp->ppqEnd; }
    // Some hosts report a valid position object but no time at all while stopped; that
    // is handled by TransportMapper (falls back to the free clock / manual tempo).
    return h;
}

void PitchLaneProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // IMPORTANT: this plugin never writes to `buffer`. Input and output share the buffer in
    // every JUCE wrapper, so the audio leaves exactly as it arrived (bit-identical).
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    if (numSamples <= 0 || mono_.empty()) return;

    if (restartFreeClock_.exchange(false)) transportMapper_.restartFreeRun();
    transportMapper_.setManualBpm(tempoParam_->load());
    detector_.setGateDb(gateParam_->load());
    detector_.setMaxAperiodicity(1.f - clarityParam_->load());

    const auto host = readHost(getPlayHead());
    const auto st = transportMapper_.update(host, numSamples);

    if (st.jumped)
    {
        samplesSinceJump_ = 0;
        smoother_.reset();
        LiveFrame marker;
        marker.songTime = st.songTime;
        marker.flags = LiveFrame::Jump | (st.playing ? LiveFrame::Playing : 0u);
        frames_.push(marker);
    }

    TransportSnapshot snap;
    snap.songTime = st.songTime;
    snap.wallMs = juce::Time::getMillisecondCounterHiRes();
    snap.bpm = st.bpm;
    snap.loopStart = st.loopStart;
    snap.loopEnd = st.loopEnd;
    snap.source = static_cast<int32_t>(st.source);
    snap.playing = st.playing;
    snap.recording = st.recording;
    snap.looping = st.looping;
    snap.hasLoop = st.hasLoop;
    snap.blockCounter = ++blockCounter_;
    transport_.store(snap);

    // Mix to mono in bounded chunks (handles hosts that exceed the announced block size).
    for (int offset = 0; offset < numSamples; offset += maxBlock_)
    {
        const int n = juce::jmin(maxBlock_, numSamples - offset);
        if (numIn <= 0)
        {
            std::fill(mono_.begin(), mono_.begin() + n, 0.f);
        }
        else
        {
            const float* ch0 = buffer.getReadPointer(0, offset);
            if (numIn == 1)
            {
                std::copy(ch0, ch0 + n, mono_.begin());
            }
            else
            {
                const float* ch1 = buffer.getReadPointer(1, offset);
                for (int i = 0; i < n; ++i) mono_[static_cast<size_t>(i)] = 0.5f * (ch0[i] + ch1[i]);
            }
        }
        analyseChunk(mono_.data(), n, st, offset);
    }
    samplesSinceJump_ += numSamples;
}

void PitchLaneProcessor::analyseChunk(const float* mono, int n, const TransportState& st, int offsetInBlock)
{
    const int got = detector_.process(mono, n, detectorFrames_.data(), static_cast<int>(detectorFrames_.size()));
    const int latency = detector_.latencySamples();
    for (int i = 0; i < got; ++i)
    {
        const auto& f = detectorFrames_[static_cast<size_t>(i)];
        const int posInBlock = offsetInBlock + f.sampleIndex;

        LiveFrame lf;
        lf.rawMidi = f.voiced ? f.midi : 0.f;
        lf.hz = f.hz;
        lf.confidence = f.confidence;
        lf.rmsDb = f.rmsDb;
        lf.midi = smoother_.process(f.voiced, f.midi);
        // Timestamp = song time of the centre of the analysed audio.
        lf.songTime = st.songTime + (posInBlock - latency) / sampleRate_;
        if (lf.midi > 0.f) lf.flags |= LiveFrame::Voiced;
        // Frames whose analysis window still contains audio from before a seek/loop wrap
        // are kept for the live readout but not placed on the timeline.
        const bool windowAfterJump = samplesSinceJump_ + posInBlock >= 2 * latency;
        if (st.playing && windowAfterJump) lf.flags |= LiveFrame::Playing;
        if (st.recording) lf.flags |= LiveFrame::Recording;
        frames_.push(lf); // drops silently if the UI is not draining (editor closed)
    }
}

juce::AudioProcessorEditor* PitchLaneProcessor::createEditor()
{
    return new PitchLaneEditor(*this);
}

void PitchLaneProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    state::writeBinary(apvts_, reference_, destData);
}

void PitchLaneProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    state::readBinary(data, sizeInBytes, apvts_, reference_);
}

} // namespace pitchlane

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new pitchlane::PitchLaneProcessor();
}
