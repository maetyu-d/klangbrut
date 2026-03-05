#include "World.h"

namespace
{
bool sameCell (juce::Vector3D<int> a, juce::Vector3D<int> b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
}

juce::Vector3D<float> Mover::getPosition (const juce::Array<Route>& routes) const
{
    if (! juce::isPositiveAndBelow (routeIndex, routes.size()))
        return {};

    if (beatInitialised)
        return { (float) beatCell.x, (float) beatCell.y, (float) beatCell.z };

    const auto& r = routes.getReference (routeIndex);
    const auto a = juce::Vector3D<float> ((float) r.start.x, (float) r.start.y, (float) r.start.z);
    const auto b = juce::Vector3D<float> ((float) r.end.x, (float) r.end.y, (float) r.end.z);
    return a + (b - a) * juce::jlimit (0.0f, 1.0f, t);
}

void Mover::stepOnBeat (const juce::Array<Route>& routes)
{
    if (! juce::isPositiveAndBelow (routeIndex, routes.size()))
        return;

    const auto& r = routes.getReference (routeIndex);
    const int dx = r.end.x - r.start.x;
    const int dy = r.end.y - r.start.y;
    const int dz = r.end.z - r.start.z;
    const int steps = juce::jmax (std::abs (dx), std::abs (dy), std::abs (dz));
    if (steps <= 0)
        return;

    auto lineCellAt = [&] (int step)
    {
        const float alpha = (float) juce::jlimit (0, steps, step) / (float) steps;
        return juce::Vector3D<int> (juce::roundToInt ((float) r.start.x + (float) dx * alpha),
                                    juce::roundToInt ((float) r.start.y + (float) dy * alpha),
                                    juce::roundToInt ((float) r.start.z + (float) dz * alpha));
    };

    if (! beatInitialised)
    {
        beatStepIndex = 0;
        beatCell = r.start;
        beatInitialised = true;
    }

    beatStepIndex = juce::jlimit (0, steps, beatStepIndex + (forward ? 1 : -1));
    beatCell = lineCellAt (beatStepIndex);

    if (beatStepIndex >= steps)
        forward = false;
    else if (beatStepIndex <= 0)
        forward = true;
}

World::World()
{
    generateDemoLevel();
}

void World::generateDemoLevel()
{
    solids.clearQuick();
    endpoints.clearQuick();
    routes.clearQuick();
    movers.clearQuick();
    triggerEvents.clearQuick();
    routeStartArmed = false;

    auto addSolid = [this] (juce::Vector3D<int> p)
    {
        if (inBounds (p) && ! hasSolid (p))
            solids.add (p);
    };

    auto fillRect = [&] (int x0, int x1, int y, int z0, int z1)
    {
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z)
                addSolid ({ x, y, z });
    };

    // Tier 0: large plaza floor.
    fillRect (1, gridX - 2, 0, 1, gridZ - 2);

    // Tier 1 platforms.
    fillRect (2, 8, 2, 2, 8);
    fillRect (11, 17, 2, 2, 8);
    fillRect (4, 15, 2, 11, 17);

    // Tier 2 terraces.
    fillRect (5, 9, 4, 5, 9);
    fillRect (11, 15, 4, 11, 15);

    // Tier 3 sky deck.
    fillRect (8, 12, 7, 8, 12);

    // Corner towers.
    for (int y = 1; y <= 9; ++y)
    {
        addSolid ({ 2, y, 2 });
        addSolid ({ 17, y, 2 });
        addSolid ({ 2, y, 17 });
        addSolid ({ 17, y, 17 });
    }

    // Central spire.
    for (int y = 1; y <= 10; ++y)
        addSolid ({ 10, y, 10 });

    // Elevated bridges between tiers.
    for (int x = 8; x <= 12; ++x) addSolid ({ x, 5, 10 });
    for (int z = 8; z <= 12; ++z) addSolid ({ 10, 5, z });

    // Zig-zag ramp (climbable visual path).
    for (int i = 0; i < 6; ++i)
    {
        addSolid ({ 3 + i, 1 + i, 14 });
        addSolid ({ 4 + i, 1 + i, 14 });
    }

    // Musical chord/note blocks across tiers.
    endpoints.add ({ { 3, 2, 3 }, { 0, 4, 7 } });
    endpoints.add ({ { 7, 4, 7 }, { 0, 3, 7, 10 } });
    endpoints.add ({ { 13, 2, 4 }, { 0, 7, 12 } });
    endpoints.add ({ { 14, 4, 14 }, { 0, 4, 7, 11 } });
    endpoints.add ({ { 10, 7, 10 }, { 0, 3, 6, 10 } });
    endpoints.add ({ { 17, 9, 17 }, { 0, 7, 12, 19 } });
    endpoints.add ({ { 2, 9, 2 }, { 0, 5, 9 } });

    // Built-in routes are orthogonal only (one axis per segment).
    auto addOrthRouteChain = [this] (juce::Vector3D<int> a, juce::Vector3D<int> b)
    {
        const juce::Vector3D<int> p1 { b.x, a.y, a.z };
        const juce::Vector3D<int> p2 { b.x, a.y, b.z };

        if (! sameCell (a, p1)) routes.add ({ a, p1 });
        if (! sameCell (p1, p2)) routes.add ({ p1, p2 });
        if (! sameCell (p2, b)) routes.add ({ p2, b });
    };

    addOrthRouteChain ({ 3, 2, 3 }, { 7, 4, 7 });
    addOrthRouteChain ({ 7, 4, 7 }, { 10, 7, 10 });
    addOrthRouteChain ({ 10, 7, 10 }, { 14, 4, 14 });
    addOrthRouteChain ({ 14, 4, 14 }, { 17, 9, 17 });
    addOrthRouteChain ({ 13, 2, 4 }, { 10, 5, 10 });
    addOrthRouteChain ({ 2, 9, 2 }, { 10, 7, 10 });

    rebuildEndpointIndex();

    // Seed a few movers so the world is alive immediately.
    for (int i = 0; i < 6; ++i)
        spawnMover();
}

void World::update (float dtSeconds)
{
    for (auto& mover : movers)
    {
        const float direction = mover.forward ? 1.0f : -1.0f;
        mover.t += direction * mover.speed * dtSeconds;

        if (mover.t >= 1.0f)
        {
            mover.t = 1.0f;
            mover.forward = false;
        }
        else if (mover.t <= 0.0f)
        {
            mover.t = 0.0f;
            mover.forward = true;
        }

        Endpoint e {};
        const auto pos = mover.getPosition (routes);
        if (nearEndpoint (pos, e))
        {
            const int root = heightToMidiRoot (e.pos.y);
            juce::Array<int> chord;
            for (const auto i : e.intervals)
                chord.add (juce::jlimit (24, 100, root + i));
            triggerEvents.add ({ pos, chord });
        }
    }

    for (int i = 0; i < movers.size(); ++i)
    {
        const auto a = movers.getReference (i).getPosition (routes);
        for (int j = i + 1; j < movers.size(); ++j)
        {
            const auto b = movers.getReference (j).getPosition (routes);
            if ((a - b).length() < 0.7f)
            {
                const auto root = juce::roundToInt ((a.y + b.y) * 0.5f);
                triggerEvents.add ({ (a + b) * 0.5f, chordAtHeight (root) });
            }
        }
    }
}

void World::stepMoversOnBeat()
{
    for (auto& mover : movers)
    {
        mover.stepOnBeat (routes);

        const auto p = mover.getPosition (routes);
        const auto c = juce::Vector3D<int> (juce::roundToInt (p.x), juce::roundToInt (p.y), juce::roundToInt (p.z));
        for (const auto& e : endpoints)
        {
            if (! sameCell (e.pos, c))
                continue;

            const int root = heightToMidiRoot (e.pos.y);
            juce::Array<int> chord;
            for (const auto i : e.intervals)
                chord.add (juce::jlimit (24, 100, root + i));
            triggerEvents.add ({ p, chord });
            break;
        }
    }

    for (int i = 0; i < movers.size(); ++i)
    {
        const auto a = movers.getReference (i).getPosition (routes);
        const auto ca = juce::Vector3D<int> (juce::roundToInt (a.x), juce::roundToInt (a.y), juce::roundToInt (a.z));
        for (int j = i + 1; j < movers.size(); ++j)
        {
            const auto b = movers.getReference (j).getPosition (routes);
            const auto cb = juce::Vector3D<int> (juce::roundToInt (b.x), juce::roundToInt (b.y), juce::roundToInt (b.z));
            if (! sameCell (ca, cb))
                continue;

            triggerEvents.add ({ (a + b) * 0.5f, chordAtHeight (ca.y) });
        }
    }
}

bool World::inBounds (juce::Vector3D<int> p) const
{
    return p.x >= 0 && p.x < gridX
        && p.y >= 0 && p.y < gridY
        && p.z >= 0 && p.z < gridZ;
}

bool World::hasSolid (juce::Vector3D<int> p) const
{
    for (const auto& s : solids)
        if (sameCell (s, p))
            return true;
    return false;
}

void World::toggleSolid (juce::Vector3D<int> p)
{
    if (! inBounds (p))
        return;

    for (int i = 0; i < solids.size(); ++i)
    {
        if (sameCell (solids.getReference (i), p))
        {
            solids.remove (i);
            return;
        }
    }

    solids.add (p);
}

void World::toggleEndpoint (juce::Vector3D<int> p, const juce::Array<int>& chord)
{
    if (! inBounds (p))
        return;

    for (int i = 0; i < endpoints.size(); ++i)
    {
        if (sameCell (endpoints.getReference (i).pos, p))
        {
            endpoints.remove (i);
            rebuildEndpointIndex();
            return;
        }
    }

    endpoints.add ({ p, chord });
    rebuildEndpointIndex();
}

void World::beginRoute (juce::Vector3D<int> start)
{
    if (! inBounds (start))
        return;

    routeStartArmed = true;
    pendingRouteStart = start;
}

bool World::commitRoute (juce::Vector3D<int> end)
{
    if (! routeStartArmed || ! inBounds (end))
        return false;

    if (sameCell (pendingRouteStart, end))
        return false;

    const int changedAxes = (pendingRouteStart.x != end.x ? 1 : 0)
                          + (pendingRouteStart.y != end.y ? 1 : 0)
                          + (pendingRouteStart.z != end.z ? 1 : 0);
    if (changedAxes != 1)
        return false;

    routes.add ({ pendingRouteStart, end });
    routeStartArmed = false;
    return true;
}

bool World::spawnMover()
{
    if (routes.isEmpty())
        return false;

    Mover m;
    m.isChordEntity = juce::Random::getSystemRandom().nextBool();
    m.routeIndex = juce::Random::getSystemRandom().nextInt (routes.size());
    m.speed = juce::jmap ((float) juce::Random::getSystemRandom().nextDouble(),
                          m.isChordEntity ? 0.08f : 0.16f,
                          m.isChordEntity ? 0.22f : 0.35f);
    movers.add (m);
    return true;
}

int World::heightToMidiRoot (int y) const
{
    return juce::jlimit (24, 100, 36 + y * 3);
}

juce::Array<int> World::chordAtHeight (int y) const
{
    const int root = heightToMidiRoot (y);
    juce::Array<int> chord { root };

    if (y % 5 == 0)
        chord.addArray ({ root + 4, root + 7, root + 11 });
    else if (y % 4 == 0)
        chord.addArray ({ root + 3, root + 7, root + 10 });
    else if (y % 3 == 0)
        chord.addArray ({ root + 5, root + 9 });
    else
        chord.addArray ({ root + 7 });

    return chord;
}

juce::Array<int> World::midiNotesForMover (const Mover& mover) const
{
    const auto pos = mover.getPosition (routes);
    const int y = juce::jlimit (0, gridY - 1, juce::roundToInt (pos.y));
    const int root = heightToMidiRoot (y);

    if (mover.isChordEntity)
        return chordAtHeight (y);

    return { root };
}

juce::Array<World::TriggerEvent> World::consumeTriggers()
{
    auto copy = triggerEvents;
    triggerEvents.clearQuick();
    return copy;
}

int World::pack (juce::Vector3D<int> p) const
{
    return p.x + p.y * 100 + p.z * 10000;
}

void World::rebuildEndpointIndex()
{
    endpointByPackedPosition.clear();
    for (int i = 0; i < endpoints.size(); ++i)
        endpointByPackedPosition.set (pack (endpoints.getReference (i).pos), i);
}

bool World::nearEndpoint (juce::Vector3D<float> p, Endpoint& result) const
{
    const auto pi = juce::Vector3D<int> (juce::roundToInt (p.x), juce::roundToInt (p.y), juce::roundToInt (p.z));
    const auto key = pack (pi);
    if (! endpointByPackedPosition.contains (key))
        return false;

    const auto idx = endpointByPackedPosition[key];
    result = endpoints.getReference (idx);
    return (p - juce::Vector3D<float> ((float) pi.x, (float) pi.y, (float) pi.z)).length() < 0.55f;
}
