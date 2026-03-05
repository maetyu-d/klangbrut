#include "SynthEngine.h"
#include <algorithm>

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
    adsrParams.attack = 0.008f;
    adsrParams.decay = 0.09f;
    adsrParams.sustain = 0.12f;
    adsrParams.release = 0.09f;
    adsr.setParameters (adsrParams);
}

void PsyVoice::setSampleRate (double sr)
{
    sampleRate = sr;
    adsr.setSampleRate (sr);

    // Soft one-pole low-pass for more melodic tone.
    const auto x = std::exp (-2.0 * juce::MathConstants<double>::pi * 2600.0 / sr);
    toneCoeff = (float) (1.0 - x);
}

void PsyVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    const auto frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
    angleDelta = juce::MathConstants<double>::twoPi * frequency / sampleRate;
    modDelta = juce::MathConstants<double>::twoPi * (frequency * 1.87) / sampleRate;
    holdSamples = 0;
    heldNoise = rng.nextFloat() * 2.0f - 1.0f;
    tonePrev = 0.0f;
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
    juce::ScopedNoDenormals noDenormals;

    if (! isVoiceActive())
        return;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto e = adsr.getNextSample();
        if (--holdSamples <= 0)
        {
            holdSamples = juce::jmax (1, juce::roundToInt ((float) sampleRate * (0.0012f + 0.0016f * rng.nextFloat())));
            heldNoise = rng.nextFloat() * 2.0f - 1.0f;
        }

        const float mod = std::sin ((float) modAngle) * 0.75f + heldNoise * 0.08f;
        const float osc1 = std::sin ((float) currentAngle + mod);
        const float osc2 = std::sin ((float) currentAngle * 2.0f + mod * 0.5f);
        const float raw = osc1 * 0.78f + osc2 * 0.22f + heldNoise * 0.03f;
        const float shaped = std::tanh (raw * 1.15f);
        tonePrev += toneCoeff * (shaped - tonePrev);
        const float v = tonePrev * level * e;

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

    for (int i = 0; i < 10; ++i)
    {
        auto* voice = new PsyVoice();
        voice->setSampleRate (sampleRate);
        synth.addVoice (voice);
    }

    synth.setNoteStealingEnabled (true);
    synth.addSound (new BasicSound());
}

void SynthEngine::render (juce::AudioBuffer<float>& audio, int startSample, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;
    juce::MidiBuffer midi;
    juce::Array<int> pumpOffsets;
    juce::Array<float> pumpAmounts;

    {
        const juce::ScopedLock sl (lock);
        const int64 blockStart = timeline;
        const int64 blockEnd = timeline + numSamples;

        while (! pending.empty() && pending.top().sampleTime < blockEnd)
        {
            const auto e = pending.top();
            pending.pop();
            if (e.sampleTime >= blockStart && e.sampleTime < blockEnd)
            {
                const int sampleOffset = (int) (e.sampleTime - blockStart);
                midi.addEvent (e.message, sampleOffset);
            }
        }

        while (! pendingPumps.empty() && pendingPumps.top().sampleTime < blockEnd)
        {
            const auto p = pendingPumps.top();
            pendingPumps.pop();
            if (p.sampleTime >= blockStart && p.sampleTime < blockEnd)
            {
                pumpOffsets.add ((int) (p.sampleTime - blockStart));
                pumpAmounts.add (p.amount);
            }
        }

        timeline += numSamples;
    }

    synth.renderNextBlock (audio, midi, startSample, numSamples);

    // Kick-driven sidechain pump for minimal-techno groove.
    const float releaseCoeff = std::exp (-1.0f / (float) (juce::jmax (1.0, sr * (sidechainReleaseMs * 0.001f))));
    int pumpIndex = 0;
    for (int s = 0; s < numSamples; ++s)
    {
        while (pumpIndex < pumpOffsets.size() && pumpOffsets.getUnchecked (pumpIndex) == s)
        {
            sidechainEnv = juce::jmax (sidechainEnv, juce::jlimit (0.0f, 1.0f, pumpAmounts.getUnchecked (pumpIndex)));
            ++pumpIndex;
        }

        const float gain = juce::jlimit (0.12f, 1.0f, 1.0f - sidechainEnv * sidechainAmount);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            const float wet = audio.getSample (ch, startSample + s) * gain * outputGain;
            // Gentle safety limiter to prevent summed-voice distortion.
            audio.setSample (ch, startSample + s, std::tanh (wet * 1.15f) * 0.88f);
        }

        sidechainEnv *= releaseCoeff;
    }
}

void SynthEngine::triggerChord (const juce::Array<int>& midiNotes, float velocity, int durationMs)
{
    triggerChordDelayed (midiNotes, 0, velocity, durationMs);
}

void SynthEngine::triggerImprovisedHarmony (const juce::Array<int>& midiNotes, float velocity, int durationMs)
{
    triggerImprovisedHarmonyDelayed (midiNotes, 0, velocity, durationMs);
}

void SynthEngine::triggerImprovisedHarmonyDelayed (const juce::Array<int>& midiNotes, int delayMs, float velocity, int durationMs)
{
    if (midiNotes.isEmpty())
        return;

    juce::Array<int> notes;
    for (const auto n : midiNotes)
        notes.add (juce::jlimit (24, 100, n));

    std::sort (notes.begin(), notes.end());
    for (int i = notes.size() - 1; i > 0; --i)
    {
        if (notes.getUnchecked (i) == notes.getUnchecked (i - 1))
            notes.remove (i);
    }

    if (notes.isEmpty())
        return;

    const float roll = rng.nextFloat();
    const int n = notes.size();

    if (n == 1 || roll < 0.72f)
    {
        const int idx = (n > 1 && rng.nextFloat() < 0.62f) ? 0 : rng.nextInt (n);
        const int main = notes.getUnchecked (idx);
        triggerChordDelayed ({ main }, delayMs, velocity * 0.95f, durationMs + 70);

        if (n > 1 && rng.nextFloat() < 0.18f)
        {
            const int ghost = notes.getUnchecked ((idx + 1) % n);
            triggerChordDelayed ({ ghost }, delayMs + juce::jmax (26, durationMs / 3), velocity * 0.48f, juce::jmax (70, durationMs / 2));
        }
        return;
    }

    if (roll < 0.96f)
    {
        const bool up = rng.nextBool();
        const int maxArpNotes = juce::jmin (2, n);
        const int stepMs = juce::jlimit (38, 120, durationMs / juce::jmax (1, maxArpNotes));
        for (int i = 0; i < maxArpNotes; ++i)
        {
            const int idx = up ? i : (n - 1 - i);
            const int note = notes.getUnchecked (idx);
            const float vel = velocity * (1.0f - 0.10f * (float) i);
            triggerChordDelayed ({ note }, delayMs + i * stepMs, juce::jmax (0.22f, vel), juce::jmax (90, durationMs + 50 - i * 14));
        }

        return;
    }

    juce::Array<int> thinChord;
    thinChord.add (notes.getUnchecked (0));
    if (n > 1)
        thinChord.add (notes.getUnchecked (juce::jmin (1, n - 1)));
    triggerChordDelayed (thinChord, delayMs, velocity * 0.72f, durationMs + 80);
}

void SynthEngine::triggerChordDelayed (const juce::Array<int>& midiNotes, int delayMs, float velocity, int durationMs)
{
    const juce::ScopedLock sl (lock);

    constexpr int kMaxPendingEvents = 10000;
    while ((int) pending.size() > kMaxPendingEvents)
        pending.pop();

    const int delaySamples = juce::jmax (0, juce::roundToInt ((double) delayMs * sr / 1000.0));
    const int noteLen = juce::jmax (1, juce::roundToInt ((double) durationMs * sr / 1000.0));
    const int spread = juce::jmax (1, juce::roundToInt (sr * 0.0045)); // more spread to reduce simultaneous peaks
    const int noteCount = juce::jmin (2, midiNotes.size()); // hard poly cap per event
    for (int i = 0; i < noteCount; ++i)
    {
        const auto n = juce::jlimit (24, 100, midiNotes.getReference (i));
        const int offset = i * spread;
        const int len = juce::jmax (1, noteLen - i * (spread / 2));
        pending.push ({ timeline + delaySamples + offset + 1, juce::MidiMessage::noteOn (1, n, velocity) });
        pending.push ({ timeline + delaySamples + offset + len, juce::MidiMessage::noteOff (1, n) });
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
    pendingPumps.push ({ timeline + delaySamples + 1, juce::jlimit (0.0f, 1.0f, amount) });
}
