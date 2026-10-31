# Macro Engine

A Geode toolkit for Geometry Dash focused on reliable input macros, playback, analysis and trajectory visualization.

## Project modules

- **Macro Recorder:** captures input transitions, timing and player identity.
- **Macro Player:** planned deterministic playback with compatibility checks.
- **Macro Analyzer:** planned event, frame and run analysis.
- **Frame Counter:** planned display normalized to a 240 FPS reference while retaining original timing.
- **Hit Borderless:** planned trajectory and collision-context visualization. It is not merely a hitbox viewer.
- **Layout Mode:** planned simplified level rendering that preserves gameplay geometry and physics.
- **Showcase Recorder:** planned tools for recording and presenting runs.

## Hit Borderless visual language

The planned full overlay uses:
- A thin yellow line for the player's sampled trajectory.
- Dark-yellow circles for press events and white circles for release events.
- Red lines for lethal collision boundaries.
- Dark-blue lines for solid/supporting surfaces.
- Green lines for orbs and other special interactive objects.

The long-term analyzer should distinguish current survival, danger and a mathematically/physically unavoidable death. It must not label a death inevitable merely because a sprite overlaps a visual decoration: collision geometry, movement state, physics and possible future inputs matter.

## Current implementation

The initial recorder scaffold is present:
- Pause-menu controls to start and stop a recording.
- Press/release event capture.
- P1/P2 identity.
- Game command-frame index and monotonic microsecond timestamps.
- PlayLayer update-step count.
- Per-update P1/P2 position samples with timestamps and command-frame references.
- Live Hit Borderless prototype: thin yellow P1/P2 trajectory strokes and dark-yellow press / white release markers.
- JSON-based `.macroengine` file output containing input events (including their positions) and sampled trajectories.

This is an early recording scaffold, not yet a complete macro system. Playback, robust file validation, imported formats, world-object collision-line rendering, and physics-based survivability analysis are not implemented. The current live trajectory overlay is an early prototype and still needs in-game visual and performance testing.

## Development order

1. Stabilize recording and the native macro data model.
2. Add a macro library, loading and validation.
3. Implement playback and compatibility safeguards.
4. Add format import/export adapters after inspecting real sample files.
5. Build timeline editing and event analysis.
6. Implement trajectory sampling and the Hit Borderless overlay.
7. Add collision/physics-based survivability analysis.
8. Add Layout Mode and Showcase Recorder.

## Timing model

Macro Engine keeps separate timing references:
- The game's command index.
- PlayLayer update steps.
- A monotonic microsecond timestamp.
- A future 240 FPS normalized display frame.

These clocks are not interchangeable. Normalized display frames must be derived from measured event timing, not by assuming the device refresh rate equals the simulation update rate.

## Build

Install the Geode SDK and CLI, then configure `GEODE_SDK` to point to the SDK directory.

```sh
geode build
```

The project currently targets Geometry Dash 2.2081 and Geode 5.11.2.
