# Block Music Toy

C++/JUCE prototype: part instrument, part toy, part 3D architectural music sandbox.

## Concept
- Minecraft-like voxel structure in a neon psychedelic style.
- Fly in full 3D (horizontal + vertical).
- Place endpoints and draw 3D routes.
- Spawn moving note/chord entities on routes.
- Sound is triggered when entities hit endpoints or collide with each other.
- Height maps to pitch/root note.

## Build

```bash
cmake -S . -B build -DBLOCK_MUSIC_JUCE_PATH=/path/to/JUCE
cmake --build build -j4
```

Alternative JUCE setup:
- Put JUCE in `./JUCE` and run the same command without the `BLOCK_MUSIC_JUCE_PATH` flag.
- Or configure `JUCE_DIR` / `CMAKE_PREFIX_PATH` for an installed JUCE package.
- Optional online fetch (if network allowed): `-DBLOCK_MUSIC_FETCH_JUCE=ON`.

## Controls
- `M`: toggle mode (`Build` / `Performance`)
- `W A S D`: player movement
- Mouse move/drag: look around (yaw/pitch)
- Cursor movement (keyboard only): `I/K` z-,z+ `J/L` x-,x+ `U/O` y+,y-
- Placement mode select only: `N` = note mode, `C` = chord mode
- `B`: place/toggle block at cursor using current mode
- After placing a chord block: chord picker opens at that cube
- Chord picker controls: `Up/Down` choose type, `Enter` confirm, `Esc` cancel
- `R`: define route (first press sets start, second press sets end)
- `P`: spawn moving musical entity on a route
- `1..5`: endpoint chord flavor

## Performance Mode
- Lines and movers become the active system.
- Strong beat/bar clock drives audio and visual pulses.
- Movers run across routes and trigger note/chord events.
- Build mode is for constructing blocks, routes, and mover setups.

## Musical Mapping
- Root note = function of Y height (`36 + y * 3`, clamped to MIDI range).
- Chord quality also varies with height bands.
- Collisions and endpoint hits trigger short synth chords.
