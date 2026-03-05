#pragma once

#include <JuceHeader.h>

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
    float hpPrevIn = 0.0f;
    float hpPrevOut = 0.0f;
    float hpCoeff = 0.0f;
    float level = 0.0f;
};

class SynthEngine
{
public:
    void prepare (double sampleRate, int maximumBlockSize, int numChannels);
    void render (juce::AudioBuffer<float>& audio, int startSample, int numSamples);

    void triggerChord (const juce::Array<int>& midiNotes, float velocity = 0.8f, int durationMs = 220);
    void triggerChordDelayed (const juce::Array<int>& midiNotes, int delayMs, float velocity = 0.8f, int durationMs = 220);
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

    juce::Synthesiser synth;
    juce::CriticalSection lock;
    juce::Array<PendingEvent> pending;
    juce::Array<PendingPump> pendingPumps;
    int64 timeline = 0;
    double sr = 44100.0;
    float sidechainEnv = 0.0f;
    float sidechainAmount = 0.85f;
    float sidechainReleaseMs = 220.0f;
};
