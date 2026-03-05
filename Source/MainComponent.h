#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "World.h"
#include "SynthEngine.h"

class MainComponent final : public juce::AudioAppComponent,
                            public juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void timerCallback() override;

private:
    struct ScreenPoint
    {
        juce::Point<float> p;
        float depth = -1.0f;
    };

    ScreenPoint project (juce::Vector3D<float> worldPoint) const;
    juce::Vector3D<float> cameraForward() const;
    juce::Vector3D<int> editCursor() const;
    void nudgeCursor (int dx, int dy, int dz);
    void applyMouseLook (juce::Point<float> currentMousePos);

    void drawCube (juce::Graphics& g, juce::Vector3D<float> c, float size, juce::Colour color, bool fill) const;
    void drawVoxelScene (juce::Graphics& g) const;
    void drawScene (juce::Graphics& g) const;
    void drawHud (juce::Graphics& g) const;
    void updateMovement (float dt);
    void setChordPlacementMode (bool enabled);
    bool sameCell (juce::Vector3D<int> a, juce::Vector3D<int> b) const;
    void updateChordMenuPosition();
    void openChordMenuForCell (juce::Vector3D<int> cell);
    void closeChordMenu();
    void resetPerformanceState();
    void schedulePerformanceBeatAudio (int sampleOffset);

    World world;
    SynthEngine synth;

    struct MusicalBlock
    {
        juce::Vector3D<int> cell;
        juce::Array<int> intervals;
    };

    enum class Mode
    {
        build,
        performance
    };

    juce::Vector3D<float> camera { 10.0f, 3.5f, -2.0f };
    float yaw = 0.2f;
    float pitch = -0.1f;
    float moveSpeed = 8.0f;
    float mouseLookSensitivity = 0.004f;

    juce::Vector3D<int> playerCursor { 10, 2, 10 };

    int selectedChord = 0;
    juce::Array<juce::Array<int>> chordPalette;
    juce::StringArray chordNames;
    bool chordPlacementMode = false;
    bool chordMenuActive = false;
    bool hasPendingChordCell = false;
    juce::Vector3D<int> pendingChordCell { 0, 0, 0 };
    juce::Label chordTypeLabel;
    juce::ComboBox chordTypeBox;

    bool firstMouseSample = true;
    juce::Point<float> lastMousePos;

    double lastTickMs = 0.0;
    double audioSampleRate = 44100.0;
    Mode mode = Mode::build;
    double performanceBpm = 128.0;
    double audioBeatPhaseSamples = 0.0;
    int audioBeatCounter = 0;
    std::atomic<int> pendingWorldBeatSteps { 0 };
    std::atomic<int> pendingBarPulseSteps { 0 };
    int beatCount = 0;
    float beatFlash = 0.0f;
    float barFlash = 0.0f;
    bool performanceViewLatched = false;
    juce::Vector3D<float> buildCameraBackup { 10.0f, 3.5f, -2.0f };
    float buildYawBackup = 0.2f;
    float buildPitchBackup = -0.1f;
};
