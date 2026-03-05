#include "SynthEngine.h"

namespace
{
class BasicSound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override     { return true; }
    bool appliesToChannel (int) override  { return true; }
};
}

PsyVoice::PsyVoice()
{
    adsrParams.attack = 0.0007f;
    adsrParams.decay = 0.026f;
    adsrParams.sustain = 0.0f;
    adsrParams.release = 0.02f;
    adsr.setParameters (adsrParams);
}

void PsyVoice::setSampleRate (double sr)
{
    sampleRate = sr;
    adsr.setSampleRate (sr);

    // One-pole HP around ~700Hz for dry click/perc emphasis.
    const auto x = std::exp (-2.0 * juce::MathConstants<double>::pi * 700.0 / sr);
    hpCoeff = (float) x;
}

void PsyVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    const auto frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
    angleDelta = juce::MathConstants<double>::twoPi * frequency / sampleRate;
    modDelta = juce::MathConstants<double>::twoPi * (frequency * 1.87) / sampleRate;
    holdSamples = 0;
    heldNoise = rng.nextFloat() * 2.0f - 1.0f;
    hpPrevIn = 0.0f;
    hpPrevOut = 0.0f;
    level = velocity;
    adsr.noteOn();
}

void PsyVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
        adsr.noteOff();
    else
        clearCurrentNote();
}

void PsyVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (! isVoiceActive())
        return;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto e = adsr.getNextSample();
        if (--holdSamples <= 0)
        {
            holdSamples = juce::jmax (1, juce::roundToInt ((float) sampleRate * (0.00025f + 0.00025f * rng.nextFloat())));
            heldNoise = rng.nextFloat() * 2.0f - 1.0f;
        }

        const float mod = std::sin ((float) modAngle) * 2.35f + heldNoise * 0.42f;
        const float carrier = std::sin ((float) currentAngle + mod);
        const float x = std::tanh ((carrier * 0.74f + heldNoise * 0.32f) * 1.6f);
        const float hp = hpCoeff * (hpPrevOut + x - hpPrevIn);
        hpPrevIn = x;
        hpPrevOut = hp;
        const float v = hp * level * e;

        for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
            outputBuffer.addSample (channel, startSample + sample, v);

        currentAngle += angleDelta;
        modAngle += modDelta;
    }

    if (! adsr.isActive())
        clearCurrentNote();
}

void SynthEngine::prepare (double sampleRate, int maximumBlockSize, int numChannels)
{
    juce::ignoreUnused (maximumBlockSize, numChannels);
    sr = sampleRate;

    synth.clearSounds();
    synth.clearVoices();

    for (int i = 0; i < 24; ++i)
    {
        auto* voice = new PsyVoice();
        voice->setSampleRate (sampleRate);
        synth.addVoice (voice);
    }

    synth.addSound (new BasicSound());
}

void SynthEngine::render (juce::AudioBuffer<float>& audio, int startSample, int numSamples)
{
    juce::MidiBuffer midi;
    juce::Array<int> pumpOffsets;
    juce::Array<float> pumpAmounts;

    {
        const juce::ScopedLock sl (lock);
        const int64 blockStart = timeline;
        const int64 blockEnd = timeline + numSamples;

        for (int i = pending.size() - 1; i >= 0; --i)
        {
            const auto& e = pending.getReference (i);
            if (e.sampleTime >= blockStart && e.sampleTime < blockEnd)
            {
                const int sampleOffset = (int) (e.sampleTime - blockStart);
                midi.addEvent (e.message, sampleOffset);
                pending.remove (i);
            }
        }

        for (int i = pendingPumps.size() - 1; i >= 0; --i)
        {
            const auto& p = pendingPumps.getReference (i);
            if (p.sampleTime >= blockStart && p.sampleTime < blockEnd)
            {
                pumpOffsets.add ((int) (p.sampleTime - blockStart));
                pumpAmounts.add (p.amount);
                pendingPumps.remove (i);
            }
        }

        timeline += numSamples;
    }

    synth.renderNextBlock (audio, midi, startSample, numSamples);

    // Kick-driven sidechain pump for minimal-techno groove.
    const float releaseCoeff = std::exp (-1.0f / (float) (juce::jmax (1.0, sr * (sidechainReleaseMs * 0.001f))));
    for (int s = 0; s < numSamples; ++s)
    {
        for (int i = 0; i < pumpOffsets.size(); ++i)
        {
            if (pumpOffsets.getUnchecked (i) == s)
                sidechainEnv = juce::jmax (sidechainEnv, juce::jlimit (0.0f, 1.0f, pumpAmounts.getUnchecked (i)));
        }

        const float gain = juce::jlimit (0.12f, 1.0f, 1.0f - sidechainEnv * sidechainAmount);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            audio.setSample (ch, startSample + s, audio.getSample (ch, startSample + s) * gain);

        sidechainEnv *= releaseCoeff;
    }
}

void SynthEngine::triggerChord (const juce::Array<int>& midiNotes, float velocity, int durationMs)
{
    triggerChordDelayed (midiNotes, 0, velocity, durationMs);
}

void SynthEngine::triggerChordDelayed (const juce::Array<int>& midiNotes, int delayMs, float velocity, int durationMs)
{
    const juce::ScopedLock sl (lock);

    const int delaySamples = juce::jmax (0, juce::roundToInt ((double) delayMs * sr / 1000.0));
    const int noteLen = juce::jmax (1, juce::roundToInt ((double) durationMs * sr / 1000.0));
    const int spread = juce::jmax (1, juce::roundToInt (sr * 0.006)); // slight offset for clustered clicks
    for (int i = 0; i < midiNotes.size(); ++i)
    {
        const auto n = midiNotes.getReference (i);
        const int offset = i * spread;
        const int len = juce::jmax (1, noteLen - i * (spread / 2));
        pending.add ({ timeline + delaySamples + offset + 1, juce::MidiMessage::noteOn (1, n, velocity) });
        pending.add ({ timeline + delaySamples + offset + len, juce::MidiMessage::noteOff (1, n) });
    }
}

void SynthEngine::sidechainPulse (float amount)
{
    sidechainPulseDelayed (0, amount);
}

void SynthEngine::sidechainPulseDelayed (int delayMs, float amount)
{
    const juce::ScopedLock sl (lock);
    const int delaySamples = juce::jmax (0, juce::roundToInt ((double) delayMs * sr / 1000.0));
    pendingPumps.add ({ timeline + delaySamples + 1, juce::jlimit (0.0f, 1.0f, amount) });
}
