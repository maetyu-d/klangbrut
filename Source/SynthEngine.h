#pragma once

#include <JuceHeader.h>
#include <queue>
#include <vector>

class PsyVoice final : public juce::SynthesiserVoice
{
public:
    PsyVoice();

    bool canPlaySound (juce::SynthesiserSound*) override { return true; }
    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    void setSampleRate (double sr);

private:
    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;
    juce::Random rng;

    double sampleRate = 44100.0;
    double currentAngle = 0.0;
    double angleDelta = 0.0;
    double modAngle = 0.0;
    double modDelta = 0.0;
    float heldNoise = 0.0f;
    int holdSamples = 0;
    float tonePrev = 0.0f;
    float toneCoeff = 0.0f;
    float level = 0.0f;
};

class SynthEngine
{
public:
    void prepare (double sampleRate, int maximumBlockSize, int numChannels);
    void render (juce::AudioBuffer<float>& audio, int startSample, int numSamples);

    void triggerChord (const juce::Array<int>& midiNotes, float velocity = 0.8f, int durationMs = 220);
    void triggerChordDelayed (const juce::Array<int>& midiNotes, int delayMs, float velocity = 0.8f, int durationMs = 220);
    void triggerImprovisedHarmony (const juce::Array<int>& midiNotes, float velocity = 0.8f, int durationMs = 220);
    void triggerImprovisedHarmonyDelayed (const juce::Array<int>& midiNotes, int delayMs, float velocity = 0.8f, int durationMs = 220);
    void sidechainPulse (float amount = 1.0f);
    void sidechainPulseDelayed (int delayMs, float amount = 1.0f);

private:
    struct PendingEvent
    {
        int64 sampleTime = 0;
        juce::MidiMessage message;
    };
    struct PendingPump
    {
        int64 sampleTime = 0;
        float amount = 0.0f;
    };
    struct EventLater
    {
        bool operator() (const PendingEvent& a, const PendingEvent& b) const noexcept { return a.sampleTime > b.sampleTime; }
        bool operator() (const PendingPump& a, const PendingPump& b) const noexcept { return a.sampleTime > b.sampleTime; }
    };

    juce::Synthesiser synth;
    juce::CriticalSection lock;
    std::priority_queue<PendingEvent, std::vector<PendingEvent>, EventLater> pending;
    std::priority_queue<PendingPump, std::vector<PendingPump>, EventLater> pendingPumps;
    juce::Random rng;
    int64 timeline = 0;
    double sr = 44100.0;
    float sidechainEnv = 0.0f;
    float sidechainAmount = 0.85f;
    float sidechainReleaseMs = 220.0f;
    float outputGain = 0.62f;
};
