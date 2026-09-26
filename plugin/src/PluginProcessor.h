#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "AnalysisManager.h"
#include "LiveData.h"
#include "Markers.h"
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
    MarkerModel& getMarkers() noexcept { return markers_; }

    /** UI side of the audio->UI pitch frame queue (single consumer: the editor). */
    bool popFrame(LiveFrame& f) noexcept { return frames_.pop(f); }
    TransportSnapshot getTransport() const noexcept { return transport_.load(); }

    /** Restart the free-running clock (used when there is no host timeline). */
    void restartFreeClock() noexcept { restartFreeClock_ = true; }

    /** Outcome of the last Analyze Vocal job (message thread only). The editor watches
        `serial` to show the result and auto-fit the roll, even if it was closed meanwhile. */
    struct AnalysisOutcome
    {
        uint32_t serial = 0;
        AnalysisManager::Status status = AnalysisManager::Status::Idle;
        juce::String message;
        juce::File file;
        int total = 0, muted = 0;
        double decodeMs = 0.0, analyzeMs = 0.0;
    };
    const AnalysisOutcome& getLastAnalysis() const noexcept { return lastAnalysis_; }

    int getOctaveConvention() const noexcept;
    double getSampleRateSafe() const noexcept { return sampleRate_; }
    int getDetectorLatencySamples() const noexcept { return detector_.latencySamples(); }

private:
    void analyseChunk(const float* mono, int n, const TransportState& st, int offsetInBlock);
    static HostPosition readHost(juce::AudioPlayHead* ph, int& timeSigNum, int& timeSigDen);
    void applySmoothing(float percent) noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    ReferenceModel reference_;
    MarkerModel markers_;
    AnalysisManager analysis_;
    AnalysisOutcome lastAnalysis_;

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
    std::atomic<float>* timeSigParam_ = nullptr;
    std::atomic<float>* noteNamesParam_ = nullptr;
    std::atomic<float>* smoothingParam_ = nullptr;
    std::atomic<float>* hostSyncParam_ = nullptr;
    float appliedSmoothing_ = -1.f;
    int timeSigNum_ = 4, timeSigDen_ = 4;
    std::atomic<bool> restartFreeClock_ { false };

    SpscRing<LiveFrame, 8192> frames_;
    SeqLockValue<TransportSnapshot> transport_;
    uint32_t blockCounter_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchLaneProcessor)
};

} // namespace pitchlane
