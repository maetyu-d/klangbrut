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

const char* modelName (PsyVoice::Model m)
{
    switch (m)
    {
        case PsyVoice::Model::malletBloom: return "Mallet Bloom";
        case PsyVoice::Model::arcadePulse: return "Arcade Pulse";
    }
    return "Mallet Bloom";
}
}

PsyVoice::Model PsyVoice::currentModel() const
{
    if (modelIndexSource == nullptr)
        return Model::malletBloom;

    const int v = juce::jlimit (0, 1, modelIndexSource->load());
    return static_cast<Model> (v);
}

PsyVoice::PsyVoice()
{
    // KlangKunst "Nova Drift"-leaning musical envelope.
    adsrParams.attack = 0.006f;
    adsrParams.decay = 0.24f;
    adsrParams.sustain = 0.38f;
    adsrParams.release = 0.30f;
    adsr.setParameters (adsrParams);
}

void PsyVoice::setSampleRate (double sr)
{
    sampleRate = sr;
    adsr.setSampleRate (sr);
}

void PsyVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    const auto model = currentModel();
    const auto frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
    angleDelta = juce::MathConstants<double>::twoPi * frequency / sampleRate;
    if (model == Model::arcadePulse) modDelta = juce::MathConstants<double>::twoPi * (frequency * 4.0) / sampleRate;
    else                             modDelta = juce::MathConstants<double>::twoPi * (frequency * 1.997) / sampleRate;

    subDelta = juce::MathConstants<double>::twoPi * (frequency * 0.5) / sampleRate;
    currentAngle = 0.0;
    modAngle = 0.0;
    subAngle = 0.0;
    lpState = 0.0f;
    hpState = 0.0f;
    noteAgeSeconds = 0.0f;
    noiseLP = 0.0f;
    noiseHP = 0.0f;
    lastNoise = 0.0f;
    noiseSeed = static_cast<uint32_t> (0x9E3779B9u ^ (static_cast<uint32_t> (midiNoteNumber) * 2654435761u));
    chipSfxType = juce::jlimit (0, 3, midiNoteNumber % 4);
    if (model == Model::malletBloom)
    {
        adsrParams.attack = 0.0004f; adsrParams.decay = 0.13f; adsrParams.sustain = 0.0f; adsrParams.release = 0.025f;
    }
    else
    {
        adsrParams.attack = 0.0001f; adsrParams.decay = 0.075f; adsrParams.sustain = 0.10f; adsrParams.release = 0.045f;
    }
    adsr.setParameters (adsrParams);
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
        const auto model = currentModel();
        const auto e = adsr.getNextSample();
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        const auto white = static_cast<float> ((noiseSeed >> 9) & 0x7FFFFFu) / 4194303.5f * 2.0f - 1.0f;
        const float transient = std::exp (-noteAgeSeconds * 24.0f);
        const float click = white * transient * 0.18f;

        float voiced = 0.0f;
        if (model == Model::malletBloom)
        {
            const auto tone = std::sin (currentAngle * 1.97 + 0.4 * std::sin (modAngle));
            const auto ping = std::sin (currentAngle * 3.9);
            const auto env2 = std::exp (-noteAgeSeconds * 14.0f);
            const auto raw = static_cast<float> (0.78 * tone + 0.16 * ping * env2 + 0.06 * click);
            lpState += 0.32f * (raw - lpState);
            voiced = std::tanh (lpState * 1.24f) * 0.68f;
        }
        else if (model == Model::arcadePulse)
        {
            static constexpr float dutySet[4] = { 0.125f, 0.25f, 0.5f, 0.75f };
            const float phase = (float) (currentAngle / juce::MathConstants<double>::twoPi);
            const float p = phase - std::floor (phase);
            const float duty = dutySet[(size_t) juce::jlimit (0, 3, chipSfxType)];
            const float pulse = (p < duty) ? 1.0f : -1.0f;
            noiseLP += 0.18f * (white - noiseLP);
            noiseHP = white - noiseLP;
            const float raw = 0.86f * pulse + 0.14f * noiseHP * std::exp (-noteAgeSeconds * 18.0f);
            lpState += 0.24f * (raw - lpState);
            voiced = std::tanh (lpState * 1.06f) * 0.70f;
        }
        const float v = voiced * level * e;

        for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
            outputBuffer.addSample (channel, startSample + sample, v);

        currentAngle += angleDelta;
        modAngle += modDelta;
        subAngle += subDelta;
        noteAgeSeconds += 1.0f / (float) sampleRate;
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
        voice->setModelSource (&modelIndex);
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

    const int n = notes.size();
    ++improvCounter;

    // KlangKunst-inspired weighted improv selector (single/chord/arp).
    const int section = (improvCounter / 16) % 5;
    const int beatSlot = improvCounter % 4;
    int singleW = 56, chordW = 30, arpW = 14;
    switch (section)
    {
        case 0: singleW = 74; chordW = 22; arpW = 4; break;
        case 1: singleW = 56; chordW = 30; arpW = 14; break;
        case 2: singleW = 40; chordW = 35; arpW = 25; break;
        case 3: singleW = 28; chordW = 48; arpW = 24; break;
        case 4: singleW = 36; chordW = 24; arpW = 40; break;
        default: break;
    }

    if (beatSlot == 0) { singleW += 10; arpW -= 8; }
    else if (beatSlot == 3) { singleW -= 10; chordW += 6; arpW += 8; }
    else if (beatSlot == 1 && section >= 2) { singleW -= 4; arpW += 6; }

    singleW = juce::jmax (5, singleW);
    chordW = juce::jmax (5, chordW);
    arpW = juce::jmax (5, arpW);
    const int totalW = singleW + chordW + arpW;
    const int pick = (improvCounter * 11 + n * 23 + section * 29) % totalW;

    enum class Style { single, chord, arp };
    Style style = Style::single;
    if (pick < singleW) style = Style::single;
    else if (pick < singleW + chordW) style = Style::chord;
    else style = Style::arp;

    if (n == 1 || style == Style::single)
    {
        const int idx = (n > 1 && rng.nextFloat() < 0.70f) ? 0 : rng.nextInt (n);
        const int note = notes.getUnchecked (idx);
        triggerChordDelayed ({ note }, delayMs, velocity * 0.96f, durationMs + 90);
        return;
    }

    if (style == Style::chord)
    {
        juce::Array<int> chord;
        chord.add (notes.getUnchecked (0));
        if (n > 1) chord.add (notes.getUnchecked (1));
        if (n > 2 && rng.nextFloat() < 0.45f) chord.add (notes.getUnchecked (2));
        triggerChordDelayed (chord, delayMs, velocity * 0.74f, durationMs + 110);
        return;
    }

    const bool up = rng.nextBool();
    const int maxArpNotes = juce::jmin (3, n);
    const int stepMs = juce::jlimit (36, 110, durationMs / juce::jmax (1, maxArpNotes));
    for (int i = 0; i < maxArpNotes; ++i)
    {
        const int idx = up ? i : (n - 1 - i);
        const int note = notes.getUnchecked (idx);
        const float vel = velocity * (1.0f - 0.10f * (float) i);
        triggerChordDelayed ({ note }, delayMs + i * stepMs, juce::jmax (0.24f, vel), juce::jmax (95, durationMs + 40 - i * 12));
    }
}

void SynthEngine::triggerChordDelayed (const juce::Array<int>& midiNotes, int delayMs, float velocity, int durationMs)
{
    const int delaySamples = juce::jmax (0, juce::roundToInt ((double) delayMs * sr / 1000.0));
    const int durationSamples = juce::jmax (1, juce::roundToInt ((double) durationMs * sr / 1000.0));
    triggerChordDelayedSamples (midiNotes, delaySamples, velocity, durationSamples);
}

void SynthEngine::triggerChordDelayedSamples (const juce::Array<int>& midiNotes, int delaySamples, float velocity, int durationSamples)
{
    const juce::ScopedLock sl (lock);

    constexpr int kMaxPendingEvents = 10000;
    while ((int) pending.size() > kMaxPendingEvents)
        pending.pop();

    const int noteLen = juce::jmax (1, durationSamples > 0 ? durationSamples : juce::roundToInt (sr * 0.22));
    const int spread = juce::jmax (1, juce::roundToInt (sr * 0.0035)); // KlangKunst-like clustered onset
    const int noteCount = juce::jmin (3, midiNotes.size()); // still capped for stability
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
    const int delaySamples = juce::jmax (0, juce::roundToInt ((double) delayMs * sr / 1000.0));
    sidechainPulseDelayedSamples (delaySamples, amount);
}

void SynthEngine::sidechainPulseDelayedSamples (int delaySamples, float amount)
{
    const juce::ScopedLock sl (lock);
    pendingPumps.push ({ timeline + delaySamples + 1, juce::jlimit (0.0f, 1.0f, amount) });
}

void SynthEngine::cycleModel()
{
    const int oldValue = modelIndex.load();
    modelIndex.store ((oldValue + 1) % 2);
}

SynthEngine::Model SynthEngine::getModel() const
{
    return static_cast<Model> (juce::jlimit (0, 1, modelIndex.load()));
}

juce::String SynthEngine::getModelName() const
{
    return modelName (static_cast<PsyVoice::Model> (juce::jlimit (0, 1, modelIndex.load())));
}
