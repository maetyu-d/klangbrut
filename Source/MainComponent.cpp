#include "MainComponent.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace
{
constexpr float kFov = 560.0f;

juce::Colour neonLerp (juce::Colour a, juce::Colour b, float t)
{
    return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t));
}

juce::Colour depthFog (juce::Colour c, float depth)
{
    const float fog = juce::jlimit (0.0f, 0.75f, (depth - 10.0f) / 48.0f);
    return c.interpolatedWith (juce::Colour::fromRGB (170, 205, 255), fog);
}

juce::Colour colourForPitchClassNewton (int midiNote)
{
    // Newton-style note-color map for pitch classes C..B.
    static const std::array<juce::Colour, 12> palette {
        juce::Colour::fromRGB (255, 40, 40),   // C
        juce::Colour::fromRGB (255, 120, 20),  // C#
        juce::Colour::fromRGB (255, 175, 30),  // D
        juce::Colour::fromRGB (255, 230, 35),  // D#
        juce::Colour::fromRGB (180, 235, 40),  // E
        juce::Colour::fromRGB (75, 205, 80),   // F
        juce::Colour::fromRGB (0, 180, 150),   // F#
        juce::Colour::fromRGB (0, 110, 220),   // G
        juce::Colour::fromRGB (75, 65, 210),   // G#
        juce::Colour::fromRGB (140, 55, 210),  // A
        juce::Colour::fromRGB (190, 45, 180),  // A#
        juce::Colour::fromRGB (235, 40, 125)   // B
    };

    const int pc = ((midiNote % 12) + 12) % 12;
    return palette[(size_t) pc];
}

struct FaceDraw
{
    juce::Path path;
    juce::Colour fill;
    juce::Colour stroke;
    int textureSeed = 0;
    int faceIndex = 0;
    int material = 0;
    juce::Array<juce::Colour> chordStripeColours;
    float depth = 0.0f;
};

enum FaceMaterial
{
    materialGround = 0,
    materialNoteBlock = 1,
    materialEndpointBlock = 2
};

uint32_t hash32 (uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

const juce::Vector3D<int> kFaceNeighbor[6] {
    { 1, 0, 0 }, { -1, 0, 0 },
    { 0, 1, 0 }, { 0, -1, 0 },
    { 0, 0, 1 }, { 0, 0, -1 }
};

const juce::Vector3D<float> kFaceNormal[6] {
    { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }
};

constexpr int kFaceVerts[6][4] {
    { 1, 5, 6, 2 }, // +x
    { 4, 0, 3, 7 }, // -x
    { 3, 2, 6, 7 }, // +y
    { 4, 5, 1, 0 }, // -y
    { 5, 4, 7, 6 }, // +z
    { 0, 1, 2, 3 }  // -z
};

}

MainComponent::MainComponent()
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    chordPalette.add ({ 0, 4, 7 });       // major
    chordPalette.add ({ 0, 3, 7 });       // minor
    chordPalette.add ({ 0, 3, 6, 10 });   // dim7
    chordPalette.add ({ 0, 4, 7, 11 });   // maj7
    chordPalette.add ({ 0, 7, 12 });      // power + octave
    chordNames.addArray ({ "Major", "Minor", "Dim7", "Maj7", "Power+8ve" });

    chordTypeLabel.setText ("Chord Type", juce::dontSendNotification);
    chordTypeLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    chordTypeLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (chordTypeLabel);

    for (int i = 0; i < chordNames.size(); ++i)
        chordTypeBox.addItem (chordNames[i], i + 1);

    chordTypeBox.setSelectedId (1, juce::dontSendNotification);
    chordTypeBox.onChange = [this]
    {
        selectedChord = juce::jlimit (0, chordPalette.size() - 1, chordTypeBox.getSelectedItemIndex());
    };
    addAndMakeVisible (chordTypeBox);
    setChordPlacementMode (false);

    setAudioChannels (0, 2);
    startTimerHz (120);
    lastTickMs = juce::Time::getMillisecondCounterHiRes();
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    synth.prepare (sampleRate, samplesPerBlockExpected, 2);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();
    synth.render (*bufferToFill.buffer, bufferToFill.startSample, bufferToFill.numSamples);
}

void MainComponent::releaseResources()
{
}

void MainComponent::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    juce::ColourGradient bg (juce::Colour::fromRGB (126, 190, 255), 0.0f, 0.0f,
                             juce::Colour::fromRGB (202, 230, 255), 0.0f, area.getBottom(), false);
    bg.addColour (0.56, juce::Colour::fromRGB (175, 220, 255));
    g.setGradientFill (bg);
    g.fillRect (area);

    // Blocky sun.
    g.setColour (juce::Colour::fromRGB (255, 244, 160).withAlpha (0.95f));
    g.fillRect (getWidth() - 170, 34, 72, 72);
    g.setColour (juce::Colour::fromRGB (255, 220, 120).withAlpha (0.85f));
    g.drawRect (getWidth() - 170, 34, 72, 72, 3);

    // Simple chunky clouds.
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 170));
    g.fillRect (70, 66, 120, 20);
    g.fillRect (96, 50, 72, 22);
    g.fillRect (260, 84, 148, 24);
    g.fillRect (292, 64, 86, 20);

    drawScene (g);
    drawHud (g);

    // Center crosshair.
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.drawLine ((float) getWidth() * 0.5f - 8.0f, (float) getHeight() * 0.5f, (float) getWidth() * 0.5f + 8.0f, (float) getHeight() * 0.5f, 2.4f);
    g.drawLine ((float) getWidth() * 0.5f, (float) getHeight() * 0.5f - 8.0f, (float) getWidth() * 0.5f, (float) getHeight() * 0.5f + 8.0f, 2.4f);
    g.setColour (juce::Colours::white.withAlpha (0.95f));
    g.drawLine ((float) getWidth() * 0.5f - 7.0f, (float) getHeight() * 0.5f, (float) getWidth() * 0.5f + 7.0f, (float) getHeight() * 0.5f, 1.2f);
    g.drawLine ((float) getWidth() * 0.5f, (float) getHeight() * 0.5f - 7.0f, (float) getWidth() * 0.5f, (float) getHeight() * 0.5f + 7.0f, 1.2f);
}

void MainComponent::resized()
{
    updateChordMenuPosition();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto cursor = editCursor();

    if (key.getTextCharacter() == 'm')
    {
        if (mode == Mode::build)
        {
            mode = Mode::performance;
            closeChordMenu();
            setChordPlacementMode (false);
            resetPerformanceState();
        }
        else
        {
            mode = Mode::build;
            closeChordMenu();
            setChordPlacementMode (false);
        }
        return true;
    }

    if (key.getTextCharacter() == 'l')
    {
        world.generateDemoLevel();
        if (mode == Mode::performance)
            resetPerformanceState();
        return true;
    }

    if (mode == Mode::performance)
        return false;

    if (chordMenuActive)
    {
        if (key.getKeyCode() == juce::KeyPress::upKey)
        {
            selectedChord = (selectedChord + chordPalette.size() - 1) % chordPalette.size();
            chordTypeBox.setSelectedItemIndex (selectedChord, juce::sendNotificationSync);
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::downKey)
        {
            selectedChord = (selectedChord + 1) % chordPalette.size();
            chordTypeBox.setSelectedItemIndex (selectedChord, juce::sendNotificationSync);
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::returnKey)
        {
            if (hasPendingChordCell)
            {
                bool hadEndpoint = false;
                for (const auto& e : world.endpoints)
                {
                    if (sameCell (e.pos, pendingChordCell))
                    {
                        hadEndpoint = true;
                        break;
                    }
                }

                const auto intervals = chordPalette.getReference (selectedChord);
                if (hadEndpoint)
                    world.toggleEndpoint (pendingChordCell, intervals);
                world.toggleEndpoint (pendingChordCell, intervals);
            }

            closeChordMenu();
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::escapeKey)
        {
            closeChordMenu();
            return true;
        }
    }

    if (key.getTextCharacter() == 'b')
    {
        if (chordPlacementMode)
        {
            if (world.hasSolid (cursor))
                world.toggleSolid (cursor);
            openChordMenuForCell (cursor);
        }
        else
        {
            for (const auto& e : world.endpoints)
            {
                if (sameCell (e.pos, cursor))
                {
                    world.toggleEndpoint (cursor, chordPalette.getReference (selectedChord));
                    break;
                }
            }
            world.toggleSolid (cursor);
        }
        return true;
    }

    if (key.getTextCharacter() == 'g')
    {
        setChordPlacementMode (true);
        return true;
    }

    if (key.getTextCharacter() == 'n')
    {
        setChordPlacementMode (false);
        return true;
    }

    if (key.getTextCharacter() == 'c')
    {
        setChordPlacementMode (true);
        return true;
    }

    if (key.getTextCharacter() == 'r')
    {
        if (! world.routeStartArmed)
            world.beginRoute (cursor);
        else
            world.commitRoute (cursor);
        return true;
    }

    if (key.getTextCharacter() == 'p')
    {
        world.spawnMover();
        return true;
    }

    if (key.getTextCharacter() >= '1' && key.getTextCharacter() <= '5')
    {
        selectedChord = juce::jlimit (0, chordPalette.size() - 1, (int) key.getTextCharacter() - (int) '1');
        chordTypeBox.setSelectedItemIndex (selectedChord, juce::sendNotificationSync);
        return true;
    }

    // Keyboard-only edit cursor control.
    if (key.getTextCharacter() == 'j') { nudgeCursor (-1, 0, 0); return true; }
    if (key.getTextCharacter() == 'l') { nudgeCursor ( 1, 0, 0); return true; }
    if (key.getTextCharacter() == 'i') { nudgeCursor ( 0, 0,-1); return true; }
    if (key.getTextCharacter() == 'k') { nudgeCursor ( 0, 0, 1); return true; }
    if (key.getTextCharacter() == 'u') { nudgeCursor ( 0, 1, 0); return true; }
    if (key.getTextCharacter() == 'o') { nudgeCursor ( 0,-1, 0); return true; }

    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    return false;
}

void MainComponent::mouseDown (const juce::MouseEvent& event)
{
    firstMouseSample = false;
    lastMousePos = event.position;
    applyMouseLook (event.position);
}

void MainComponent::mouseMove (const juce::MouseEvent& event)
{
    applyMouseLook (event.position);
}

void MainComponent::mouseDrag (const juce::MouseEvent& event)
{
    applyMouseLook (event.position);
}

void MainComponent::mouseUp (const juce::MouseEvent&)
{
}

void MainComponent::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = (float) ((now - lastTickMs) * 0.001);
    lastTickMs = now;

    updateMovement (dt);
    if (mode == Mode::build)
    {
        beatFlash = 0.0f;
        barFlash = 0.0f;
        // Build mode: edit-only, no running simulation.
    }
    else
    {
        beatFlash *= std::exp (-dt * 9.0f);
        barFlash *= std::exp (-dt * 3.8f);
        const double beatIntervalMs = 60000.0 / performanceBpm;
        if (nextBeatTimeMs <= 0.0)
            nextBeatTimeMs = now;

        while (now + beatLookaheadMs >= nextBeatTimeMs)
        {
            const int delayMs = juce::jmax (0, juce::roundToInt (nextBeatTimeMs - now));
            performanceBeatStep (delayMs);
            nextBeatTimeMs += beatIntervalMs;
        }
        const auto triggers = world.consumeTriggers();
        const int triggerLimit = juce::jmin (3, triggers.size());
        for (int i = 0; i < triggerLimit; ++i)
        {
            const auto& trig = triggers.getReference (i);
            juce::ignoreUnused (trig.position);
            synth.triggerImprovisedHarmony (trig.chord, 0.40f, 120);
        }
    }

    updateChordMenuPosition();
    repaint();
}

MainComponent::ScreenPoint MainComponent::project (juce::Vector3D<float> worldPoint) const
{
    auto p = worldPoint - camera;

    const float cosy = std::cos (-yaw);
    const float siny = std::sin (-yaw);
    const float px = p.x * cosy - p.z * siny;
    const float pz = p.x * siny + p.z * cosy;
    p.x = px;
    p.z = pz;

    const float cosp = std::cos (-pitch);
    const float sinp = std::sin (-pitch);
    const float py = p.y * cosp - p.z * sinp;
    const float pz2 = p.y * sinp + p.z * cosp;
    p.y = py;
    p.z = pz2;

    if (p.z <= 0.1f)
        return {};

    const float sx = (float) getWidth() * 0.5f + (p.x / p.z) * kFov;
    const float sy = (float) getHeight() * 0.5f - (p.y / p.z) * kFov;
    return { { sx, sy }, p.z };
}

juce::Vector3D<float> MainComponent::cameraForward() const
{
    const float cp = std::cos (pitch);
    return { std::sin (yaw) * cp, std::sin (pitch), std::cos (yaw) * cp };
}

void MainComponent::applyMouseLook (juce::Point<float> currentMousePos)
{
    if (firstMouseSample)
    {
        firstMouseSample = false;
        lastMousePos = currentMousePos;
        return;
    }

    const auto delta = currentMousePos - lastMousePos;
    lastMousePos = currentMousePos;

    yaw += delta.x * mouseLookSensitivity;
    pitch -= delta.y * mouseLookSensitivity;
    pitch = juce::jlimit (-1.2f, 1.2f, pitch);
}

void MainComponent::nudgeCursor (int dx, int dy, int dz)
{
    playerCursor.x = juce::jlimit (0, world.gridX - 1, playerCursor.x + dx);
    playerCursor.y = juce::jlimit (0, world.gridY - 1, playerCursor.y + dy);
    playerCursor.z = juce::jlimit (0, world.gridZ - 1, playerCursor.z + dz);
}

void MainComponent::setChordPlacementMode (bool enabled)
{
    chordPlacementMode = enabled;
    if (! enabled)
        closeChordMenu();
}

void MainComponent::openChordMenuForCell (juce::Vector3D<int> cell)
{
    pendingChordCell = cell;
    hasPendingChordCell = true;
    chordMenuActive = true;
    chordTypeLabel.setVisible (true);
    chordTypeBox.setVisible (true);
    chordTypeBox.setSelectedItemIndex (selectedChord, juce::dontSendNotification);
    updateChordMenuPosition();
}

void MainComponent::closeChordMenu()
{
    chordMenuActive = false;
    hasPendingChordCell = false;
    chordTypeLabel.setVisible (false);
    chordTypeBox.setVisible (false);
}

bool MainComponent::sameCell (juce::Vector3D<int> a, juce::Vector3D<int> b) const
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

void MainComponent::resetPerformanceState()
{
    nextBeatTimeMs = juce::Time::getMillisecondCounterHiRes();
    beatCount = 0;
    beatFlash = 0.0f;
    barFlash = 0.0f;
}

void MainComponent::performanceBeatStep (int baseDelayMs)
{
    ++beatCount;
    world.stepMoversOnBeat();

    const int beatMs = juce::roundToInt (60000.0 / performanceBpm);
    const int halfBeatMs = juce::jmax (1, beatMs / 2);
    const int quarterBeatMs = juce::jmax (1, beatMs / 4);
    const int threeQuarterBeatMs = juce::jmax (1, (beatMs * 3) / 4);
    const int beatInBar = (beatCount - 1) % 4;
    const bool isBarDownbeat = (beatInBar == 0);
    const bool isPreBarBeat = (beatInBar == 3);
    beatFlash = 1.0f;
    if (isBarDownbeat)
        barFlash = 1.0f;

    // Drum bed only here; melodic content is mover/line-triggered.
    synth.triggerChordDelayed ({ 32 }, baseDelayMs, isBarDownbeat ? 1.0f : 0.92f, isBarDownbeat ? 110 : 92); // kick
    synth.sidechainPulseDelayed (baseDelayMs, isBarDownbeat ? 1.0f : 0.84f);
    synth.triggerChordDelayed ({ 37 }, baseDelayMs + quarterBeatMs, 0.25f, 44);                               // low percussion tick
    synth.triggerChordDelayed ({ 50 }, baseDelayMs + halfBeatMs, 0.22f, 22);                                  // muted hat
    synth.triggerChordDelayed ({ 52 }, baseDelayMs + threeQuarterBeatMs, 0.16f, 18);                          // muted lift

    if (beatInBar == 1 || beatInBar == 3)
        synth.triggerChordDelayed ({ 48 }, baseDelayMs + halfBeatMs, 0.30f, 36);                              // mid accent

    if (isBarDownbeat)
    {
        // Strong bar marker.
        synth.triggerChordDelayed ({ 27 }, baseDelayMs, 0.88f, 160);
        synth.sidechainPulseDelayed (baseDelayMs, 0.7f);
        synth.triggerChordDelayed ({ 45 }, baseDelayMs + halfBeatMs, 0.14f, 24);
    }

    if (isPreBarBeat)
        synth.triggerChordDelayed ({ 47 }, baseDelayMs + halfBeatMs, 0.14f, 18);

    // Melodic content comes from mover collisions/endpoints only.
}

void MainComponent::updateChordMenuPosition()
{
    if (! chordMenuActive || ! hasPendingChordCell)
        return;

    const int w = 180;
    const int h = 24;
    const auto anchor = project ({ (float) pendingChordCell.x, (float) pendingChordCell.y + 0.5f, (float) pendingChordCell.z });

    int x = getWidth() - w - 16;
    int y = getHeight() - 140;

    if (anchor.depth > 0.0f)
    {
        x = juce::roundToInt (anchor.p.x) + 18;
        y = juce::roundToInt (anchor.p.y) - 36;
        x = juce::jlimit (10, juce::jmax (10, getWidth() - w - 10), x);
        y = juce::jlimit (10, juce::jmax (10, getHeight() - 80), y);
    }

    chordTypeLabel.setBounds (x, y, w, h);
    chordTypeBox.setBounds (x, y + h + 2, w, h);
}

juce::Vector3D<int> MainComponent::editCursor() const
{
    return playerCursor;
}

void MainComponent::drawCube (juce::Graphics& g, juce::Vector3D<float> c, float size, juce::Colour color, bool fill) const
{
    const float h = size * 0.5f;
    juce::Vector3D<float> v[8] {
        { c.x - h, c.y - h, c.z - h }, { c.x + h, c.y - h, c.z - h },
        { c.x + h, c.y + h, c.z - h }, { c.x - h, c.y + h, c.z - h },
        { c.x - h, c.y - h, c.z + h }, { c.x + h, c.y - h, c.z + h },
        { c.x + h, c.y + h, c.z + h }, { c.x - h, c.y + h, c.z + h }
    };

    ScreenPoint p[8];
    for (int i = 0; i < 8; ++i)
    {
        p[i] = project (v[i]);
        if (p[i].depth <= 0.0f)
            return;
    }

    const int edges[12][2] {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
    };

    if (fill)
    {
        juce::Path front;
        front.startNewSubPath (p[4].p);
        front.lineTo (p[5].p);
        front.lineTo (p[6].p);
        front.lineTo (p[7].p);
        front.closeSubPath();

        g.setColour (color.withAlpha (0.28f));
        g.fillPath (front);
    }

    g.setColour (color.withAlpha (0.92f));
    for (const auto& e : edges)
        g.drawLine ({ p[e[0]].p.x, p[e[0]].p.y, p[e[1]].p.x, p[e[1]].p.y }, 1.5f);
}

void MainComponent::drawVoxelScene (juce::Graphics& g) const
{
    juce::Array<FaceDraw> faces;
    auto voxelCenter = [] (juce::Vector3D<int> c) { return juce::Vector3D<float> ((float) c.x, (float) c.y + 0.5f, (float) c.z); };

    auto hasAnyVoxelAt = [&] (juce::Vector3D<int> c) {
        if (world.hasSolid (c))
            return true;
        return std::any_of (world.endpoints.begin(), world.endpoints.end(),
                            [&] (const Endpoint& e) { return e.pos.x == c.x && e.pos.y == c.y && e.pos.z == c.z; });
    };

    auto emitCube = [&] (juce::Vector3D<float> center,
                         float size,
                         juce::Colour baseColour,
                         std::optional<juce::Vector3D<int>> cell,
                         int material,
                         const juce::Array<juce::Colour>& stripeColours) {
        const float h = size * 0.5f;
        juce::Vector3D<float> v[8] {
            { center.x - h, center.y - h, center.z - h }, { center.x + h, center.y - h, center.z - h },
            { center.x + h, center.y + h, center.z - h }, { center.x - h, center.y + h, center.z - h },
            { center.x - h, center.y - h, center.z + h }, { center.x + h, center.y - h, center.z + h },
            { center.x + h, center.y + h, center.z + h }, { center.x - h, center.y + h, center.z + h }
        };

        auto toCameraSpace = [this] (juce::Vector3D<float> worldPoint) {
            auto p = worldPoint - camera;

            const float cosy = std::cos (-yaw);
            const float siny = std::sin (-yaw);
            const float px = p.x * cosy - p.z * siny;
            const float pz = p.x * siny + p.z * cosy;
            p.x = px;
            p.z = pz;

            const float cosp = std::cos (-pitch);
            const float sinp = std::sin (-pitch);
            const float py = p.y * cosp - p.z * sinp;
            const float pz2 = p.y * sinp + p.z * cosp;
            p.y = py;
            p.z = pz2;
            return p;
        };

        juce::Vector3D<float> camV[8];
        for (int i = 0; i < 8; ++i)
            camV[i] = toCameraSpace (v[i]);

        for (int f = 0; f < 6; ++f)
        {
            if (cell.has_value() && hasAnyVoxelAt (*cell + kFaceNeighbor[f]))
                continue;

            const auto& c0 = camV[kFaceVerts[f][0]];
            const auto& c1 = camV[kFaceVerts[f][1]];
            const auto& c2 = camV[kFaceVerts[f][2]];
            juce::ignoreUnused (c0, c1, c2);

            ScreenPoint sp[4];
            float depth = 0.0f;
            bool valid = true;
            for (int i = 0; i < 4; ++i)
            {
                sp[i] = project (v[kFaceVerts[f][i]]);
                if (sp[i].depth <= 0.0f)
                {
                    valid = false;
                    break;
                }
                depth = juce::jmax (depth, sp[i].depth);
            }

            if (! valid)
                continue;

            juce::Path path;
            path.startNewSubPath (sp[0].p);
            path.lineTo (sp[1].p);
            path.lineTo (sp[2].p);
            path.lineTo (sp[3].p);
            path.closeSubPath();

            const auto lit = baseColour.withMultipliedBrightness (f == 2 ? 1.15f : (f == 3 ? 0.62f : 0.88f)).withAlpha (1.0f);
            const auto foggedFill = depthFog (lit, depth);
            const auto foggedStroke = depthFog (baseColour.darker (0.65f).withAlpha (0.55f), depth);
            const auto sx = (uint32_t) juce::roundToInt (center.x * 31.0f);
            const auto sy = (uint32_t) juce::roundToInt (center.y * 47.0f);
            const auto sz = (uint32_t) juce::roundToInt (center.z * 59.0f);
            const auto seedU = sx * 73856093U ^ sy * 19349663U ^ sz * 83492791U ^ (uint32_t) f * 2654435761U;
            const int seed = (int) (seedU & 0x7fffffffU);
            faces.add ({ std::move (path), foggedFill, foggedStroke, seed, f, material, stripeColours, depth });
        }
    };

    for (int x = 0; x < world.gridX; ++x)
    {
        for (int z = 0; z < world.gridZ; ++z)
        {
            emitCube ({ (float) x, -0.5f, (float) z }, 1.0f, juce::Colour::fromRGB (110, 175, 70), std::nullopt, materialGround, {});
        }
    }

    for (const auto& s : world.solids)
    {
        const int midi = world.heightToMidiRoot (s.y);
        const auto noteColour = colourForPitchClassNewton (midi);
        emitCube (voxelCenter (s), 1.0f, noteColour, s, materialNoteBlock, {});
    }

    for (const auto& e : world.endpoints)
    {
        const int midi = world.heightToMidiRoot (e.pos.y);
        const auto noteColour = colourForPitchClassNewton (midi).brighter (0.25f);
        juce::Array<juce::Colour> stripes;
        for (const auto interval : e.intervals)
            stripes.add (colourForPitchClassNewton (midi + interval));
        emitCube (voxelCenter (e.pos), 1.0f, noteColour, e.pos, materialEndpointBlock, stripes);
    }

    std::stable_sort (faces.begin(), faces.end(), [] (const FaceDraw& a, const FaceDraw& b)
    {
        if (a.depth != b.depth)
            return a.depth > b.depth;
        return a.textureSeed < b.textureSeed;
    });

    for (const auto& f : faces)
    {
        g.setColour (f.fill);
        g.fillPath (f.path);

        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (f.path);

            const auto bounds = f.path.getBounds();

            if (f.material == materialEndpointBlock && ! f.chordStripeColours.isEmpty())
            {
                const int bands = f.chordStripeColours.size();
                for (int i = 0; i < bands; ++i)
                {
                    g.setColour (f.chordStripeColours[i].withAlpha (0.88f));
                    const float bandW = bounds.getWidth() / (float) bands;
                    g.fillRect (bounds.getX() + bandW * (float) i, bounds.getY(), bandW + 1.0f, bounds.getHeight());
                }
            }

            // Keep texturing O(1) per face to avoid frame stalls.
            if (f.depth < 42.0f)
            {
                const int patches = f.material == materialGround ? 4 : 3;
                for (int i = 0; i < patches; ++i)
                {
                    const auto h = hash32 ((uint32_t) (f.textureSeed ^ (i * 2246822519U)));
                    const float nx = (float) (h & 255U) / 255.0f;
                    const float ny = (float) ((h >> 8) & 255U) / 255.0f;
                    const float ns = (float) ((h >> 16) & 255U) / 255.0f;
                    const float nt = (float) ((h >> 24) & 255U) / 255.0f;

                    juce::Colour px = f.fill;
                    if (f.material == materialGround)
                    {
                        if (f.faceIndex == 2)       px = (nt < 0.45f ? f.fill.brighter (0.16f) : f.fill.darker (0.12f));
                        else if (f.faceIndex == 3)  px = (nt < 0.5f ? juce::Colour::fromRGB (122, 95, 62) : juce::Colour::fromRGB (104, 80, 53));
                        else                        px = (nt < 0.5f ? juce::Colour::fromRGB (137, 106, 69) : juce::Colour::fromRGB (113, 89, 60));
                    }
                    else if (f.material == materialEndpointBlock)
                    {
                        px = (nt < 0.5f ? f.fill.brighter (0.23f) : f.fill.darker (0.24f));
                    }
                    else
                    {
                        px = (nt < 0.5f ? f.fill.brighter (0.20f) : f.fill.darker (0.22f));
                    }

                    const float w = juce::jlimit (3.0f, 14.0f, 3.0f + ns * 11.0f);
                    const float hsize = juce::jlimit (3.0f, 14.0f, 3.0f + (1.0f - ns) * 11.0f);
                    const float x = bounds.getX() + nx * juce::jmax (1.0f, bounds.getWidth() - w);
                    const float y = bounds.getY() + ny * juce::jmax (1.0f, bounds.getHeight() - hsize);

                    g.setColour (px.withAlpha (0.9f));
                    g.fillRect (x, y, w, hsize);
                }
            }
        }

        if (f.material == materialGround)
        {
            g.setColour (f.stroke.withAlpha (0.45f));
            g.strokePath (f.path, juce::PathStrokeType (0.7f));
        }
    }
}

void MainComponent::drawScene (juce::Graphics& g) const
{
    auto voxelCenterF = [] (juce::Vector3D<int> c) { return juce::Vector3D<float> ((float) c.x, (float) c.y + 0.5f, (float) c.z); };
    drawVoxelScene (g);

    for (const auto& r : world.routes)
    {
        const auto a = project (voxelCenterF (r.start));
        const auto b = project (voxelCenterF (r.end));
        if (a.depth > 0.0f && b.depth > 0.0f)
        {
    g.setColour (juce::Colour::fromRGB (70, 50, 20).withAlpha (0.55f));
    g.drawLine ({ a.p.x, a.p.y, b.p.x, b.p.y }, 4.6f);
    g.setColour (juce::Colour::fromRGB (255, 245, 160).withAlpha (0.92f));
    g.drawLine ({ a.p.x, a.p.y, b.p.x, b.p.y }, 2.2f);
        }
    }

    for (const auto& m : world.movers)
    {
        auto pos = m.getPosition (world.routes);
        pos.y += 0.5f;
        const auto midiNotes = world.midiNotesForMover (m);
        const float phase = (float) juce::Time::currentTimeMillis() * 0.0024f;

        if (m.isChordEntity)
        {
            const auto chordColour = colourForPitchClassNewton (midiNotes[0]);
            const float pulse = 0.54f + 0.08f * std::sin (phase);
            drawCube (g, pos, pulse, chordColour, true);
            drawCube (g, pos, pulse + 0.16f, chordColour.withAlpha (0.6f), false);

            const auto c2d = project (pos);
            if (c2d.depth > 0.0f)
            {
                const float radius = juce::jlimit (8.0f, 34.0f, 165.0f / c2d.depth) + 2.5f * std::sin (phase);
                const int slices = juce::jmax (1, midiNotes.size());

                for (int i = 0; i < slices; ++i)
                {
                    const float a0 = juce::MathConstants<float>::twoPi * ((float) i / (float) slices) - juce::MathConstants<float>::halfPi;
                    const float a1 = juce::MathConstants<float>::twoPi * ((float) (i + 1) / (float) slices) - juce::MathConstants<float>::halfPi;

                    juce::Path seg;
                    seg.addPieSegment (c2d.p.x - radius, c2d.p.y - radius, radius * 2.0f, radius * 2.0f, a0, a1, 0.27f);
                    g.setColour (colourForPitchClassNewton (midiNotes[i]).withAlpha (0.92f));
                    g.fillPath (seg);
                }

                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.drawEllipse (c2d.p.x - radius, c2d.p.y - radius, radius * 2.0f, radius * 2.0f, 1.4f);
            }
        }
        else
        {
            const auto noteColour = colourForPitchClassNewton (midiNotes[0]);
            const float pulse = 0.36f + 0.04f * std::sin (phase * 1.9f);
            drawCube (g, pos, pulse, noteColour, true);

            const auto a = project ({ pos.x - 0.38f, pos.y, pos.z });
            const auto b = project ({ pos.x + 0.38f, pos.y, pos.z });
            const auto c = project ({ pos.x, pos.y + 0.38f, pos.z });
            const auto d = project ({ pos.x, pos.y - 0.38f, pos.z });
            if (a.depth > 0.0f && b.depth > 0.0f && c.depth > 0.0f && d.depth > 0.0f)
            {
                g.setColour (noteColour.brighter (0.4f).withAlpha (0.9f));
                g.drawLine ({ a.p.x, a.p.y, b.p.x, b.p.y }, 1.6f);
                g.drawLine ({ c.p.x, c.p.y, d.p.x, d.p.y }, 1.6f);
            }
        }
    }

    if (mode == Mode::performance)
    {
        for (int i = 0; i < world.movers.size(); ++i)
        {
            const auto pos = world.movers.getReference (i).getPosition (world.routes);
            const float emphasis = (i == 0 ? (beatFlash * 0.52f + barFlash * 0.95f) : (beatFlash * 0.20f + barFlash * 0.30f));
            const float scale = 0.35f * (1.0f + emphasis);
            drawCube (g, { pos.x, pos.y + 0.5f, pos.z }, scale, juce::Colour::fromRGB (70, 255, 220), false);
        }
    }

    const auto cursor = editCursor();
    drawCube (g, { (float) cursor.x, (float) cursor.y + 0.5f, (float) cursor.z }, 1.05f, juce::Colour::fromRGB (255, 255, 255), false);
}

void MainComponent::drawHud (juce::Graphics& g) const
{
    const auto bottom = getLocalBounds().removeFromBottom (152).toFloat();
    g.setColour (juce::Colour::fromRGBA (0, 0, 0, 120));
    g.fillRect (bottom);

    const auto yCol = neonLerp (juce::Colour::fromRGB (80, 200, 255), juce::Colour::fromRGB (255, 90, 170), (float) camera.y / (float) world.gridY);
    g.setColour (yCol);
    g.setFont (juce::Font (16.0f, juce::Font::bold));
    g.drawText ("Block Music Toy // Rez x Minecraft // Height = pitch root", 16, getHeight() - 130, getWidth() - 24, 26, juce::Justification::left);

    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.setFont (juce::Font (14.0f));
    g.drawText ("Movement: WASD only | Look: mouse move/drag", 16, getHeight() - 112, getWidth() - 24, 20, juce::Justification::left);
    g.drawText ("Placement: N note mode, C chord mode, B place | Chord picker: Up/Down + Enter (opens after chord place)", 16, getHeight() - 92, getWidth() - 24, 20, juce::Justification::left);
    g.drawText ("Modes: M toggles Build/Performance", 16, getHeight() - 72, getWidth() - 24, 20, juce::Justification::left);

    if (mode == Mode::performance)
    {
        const auto meter = juce::Rectangle<float> (16.0f, (float) getHeight() - 18.0f, 220.0f, 8.0f);
        g.setColour (juce::Colour::fromRGBA (0, 0, 0, 140));
        g.fillRect (meter);
        g.setColour (juce::Colour::fromRGB (120, 235, 255).withAlpha (0.8f));
        g.fillRect (meter.withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, beatFlash)));
        g.setColour (juce::Colour::fromRGB (255, 90, 180).withAlpha (0.85f));
        g.fillRect (meter.withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, barFlash)));
    }

    const auto c = editCursor();
    g.drawText ("Cursor " + juce::String (c.x) + "," + juce::String (c.y) + "," + juce::String (c.z)
              + " | Routes: " + juce::String (world.routes.size())
              + " | Movers: " + juce::String (world.movers.size())
              + " | PlaceMode: " + juce::String (chordPlacementMode ? "Chord" : "Note")
              + " | Mode: " + juce::String (mode == Mode::build ? "Build" : "Performance")
              + " | CameraY: " + juce::String (camera.y, 2),
               16, getHeight() - 42, getWidth() - 24, 20, juce::Justification::left);

    // Minecraft-like hotbar.
    const int slotW = 42;
    const int slotH = 42;
    const int slots = 5;
    const int totalW = slots * slotW + (slots - 1) * 6;
    const int startX = getWidth() / 2 - totalW / 2;
    const int y = getHeight() - 48;
    for (int i = 0; i < slots; ++i)
    {
        auto r = juce::Rectangle<int> (startX + i * (slotW + 6), y, slotW, slotH);
        g.setColour (juce::Colour::fromRGBA (35, 35, 35, 210));
        g.fillRect (r);
        g.setColour (i == selectedChord ? juce::Colour::fromRGB (255, 245, 180) : juce::Colour::fromRGB (120, 120, 120));
        g.drawRect (r, i == selectedChord ? 3 : 2);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.drawText (juce::String (i + 1), r, juce::Justification::centred);
    }
}

void MainComponent::updateMovement (float dt)
{
    const auto lookFwd = cameraForward().normalised();

    // Keep W/S level near horizon; only add vertical travel when look pitch is clearly up/down.
    auto moveFwd = lookFwd;
    moveFwd.y = -moveFwd.y;
    if (std::abs (moveFwd.y) < 0.28f)
        moveFwd.y = 0.0f;
    moveFwd = moveFwd.normalised();

    auto right = juce::Vector3D<float> (lookFwd.z, 0.0f, -lookFwd.x);
    if (right.length() > 0.0001f)
        right = right.normalised();
    else
        right = { 1.0f, 0.0f, 0.0f };

    const float speed = moveSpeed * dt;

    if (juce::KeyPress::isKeyCurrentlyDown ('w')) camera += moveFwd * speed;
    if (juce::KeyPress::isKeyCurrentlyDown ('s')) camera -= moveFwd * speed;
    if (juce::KeyPress::isKeyCurrentlyDown ('a')) camera -= right * speed;
    if (juce::KeyPress::isKeyCurrentlyDown ('d')) camera += right * speed;

    pitch = juce::jlimit (-1.2f, 1.2f, pitch);
    camera.x = juce::jlimit (-3.0f, (float) world.gridX + 3.0f, camera.x);
    camera.y = juce::jlimit (-2.0f, (float) world.gridY + 4.0f, camera.y);
    camera.z = juce::jlimit (-4.0f, (float) world.gridZ + 4.0f, camera.z);
}
