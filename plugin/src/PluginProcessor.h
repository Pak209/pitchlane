#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "AnalysisManager.h"
#include "LiveData.h"
#include "ReferenceModel.h"
#include "pitchlane/PitchDetector.h"
#include "pitchlane/PitchSmoother.h"
#include "pitchlane/SpscRing.h"
#include "pitchlane/Transport.h"

namespace pitchlane {

class PitchLaneProcessor : public juce::AudioProcessor
{
public:
    PitchLaneProcessor();
    ~PitchLaneProcessor() override;

    // ---- AudioProcessor ------------------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Pitch Lane"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- shared with the UI -----------------------------------------------------------
    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts_; }
    ReferenceModel& getReference() noexcept { return reference_; }
    AnalysisManager& getAnalysis() noexcept { return analysis_; }

    /** UI side of the audio->UI pitch frame queue (single consumer: the editor). */
    bool popFrame(LiveFrame& f) noexcept { return frames_.pop(f); }
    TransportSnapshot getTransport() const noexcept { return transport_.load(); }

    /** Restart the free-running clock (used when there is no host timeline). */
    void restartFreeClock() noexcept { restartFreeClock_ = true; }

    int getOctaveConvention() const noexcept;
    double getSampleRateSafe() const noexcept { return sampleRate_; }
    int getDetectorLatencySamples() const noexcept { return detector_.latencySamples(); }

private:
    void analyseChunk(const float* mono, int n, const TransportState& st, int offsetInBlock);
    static HostPosition readHost(juce::AudioPlayHead* ph);

    juce::AudioProcessorValueTreeState apvts_;
    ReferenceModel reference_;
    AnalysisManager analysis_;

    // Audio-thread state (all allocated in prepareToPlay).
    PitchDetector detector_;
    PitchSmoother smoother_;
    TransportMapper transportMapper_;
    std::vector<float> mono_;
    std::vector<PitchFrame> detectorFrames_;
    double sampleRate_ = 44100.0;
    int maxBlock_ = 0;
    int64_t samplesSinceJump_ = 0;

    std::atomic<float>* gateParam_ = nullptr;
    std::atomic<float>* clarityParam_ = nullptr;
    std::atomic<float>* tempoParam_ = nullptr;
    std::atomic<float>* noteNamesParam_ = nullptr;
    std::atomic<bool> restartFreeClock_ { false };

    SpscRing<LiveFrame, 8192> frames_;
    SeqLockValue<TransportSnapshot> transport_;
    uint32_t blockCounter_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchLaneProcessor)
};

} // namespace pitchlane
