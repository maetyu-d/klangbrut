#pragma once

#include <JuceHeader.h>

struct Route
{
    juce::Vector3D<int> start;
    juce::Vector3D<int> end;
};

struct Endpoint
{
    juce::Vector3D<int> pos;
    juce::Array<int> intervals;
};

struct Mover
{
    bool isChordEntity = false;
    int routeIndex = -1;
    float t = 0.0f;
    float speed = 0.16f;
    bool forward = true;
    bool beatInitialised = false;
    juce::Vector3D<int> beatCell;
    int beatStepIndex = 0;

    juce::Vector3D<float> getPosition (const juce::Array<Route>& routes) const;
    void stepOnBeat (const juce::Array<Route>& routes);
};

class World
{
public:
    World();
    void generateDemoLevel();

    void update (float dtSeconds);
    void stepMoversOnBeat();

    bool inBounds (juce::Vector3D<int> p) const;
    bool hasSolid (juce::Vector3D<int> p) const;
    void toggleSolid (juce::Vector3D<int> p);

    void toggleEndpoint (juce::Vector3D<int> p, const juce::Array<int>& chord);

    void beginRoute (juce::Vector3D<int> start);
    bool commitRoute (juce::Vector3D<int> end);

    bool spawnMover();

    int heightToMidiRoot (int y) const;
    juce::Array<int> chordAtHeight (int y) const;
    juce::Array<int> midiNotesForMover (const Mover& mover) const;

    juce::Array<Route> routes;
    juce::Array<Endpoint> endpoints;
    juce::Array<Mover> movers;

    juce::Array<juce::Vector3D<int>> solids;

    int gridX = 20;
    int gridY = 12;
    int gridZ = 20;

    bool routeStartArmed = false;
    juce::Vector3D<int> pendingRouteStart;

    struct TriggerEvent
    {
        juce::Vector3D<float> position;
        juce::Array<int> chord;
    };

    juce::Array<TriggerEvent> consumeTriggers();

private:
    juce::Array<TriggerEvent> triggerEvents;
    juce::HashMap<int, int> endpointByPackedPosition;

    int pack (juce::Vector3D<int> p) const;
    void rebuildEndpointIndex();
    bool nearEndpoint (juce::Vector3D<float> p, Endpoint& result) const;
};
