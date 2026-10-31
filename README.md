# Macro Engine

A Geode toolkit for Geometry Dash focused on input recording, macro playback, timing analysis and trajectory visualization.

## Current status

### Macro Recorder
- Pause-menu control to start and stop recording.
- Captures Press/Release transitions, button ID and P1/P2 identity.
- Stores the game's command index and a monotonic microsecond timestamp for every input.
- Samples P1/P2 positions during gameplay.
- Saves .macroengine JSON files in the mod's save directory under macros/.
- Uses a temporary file followed by a rename so an interrupted write is less likely to leave a half-written final macro.
- Timestamped filenames include milliseconds to reduce collisions.

### Macro Player — first functional prototype
- Loads the newest .macroengine file from the macro library.
- Validates JSON format, schema version, event types, finite positions, chronological order and safety limits before accepting a file.
- Schedules Press/Release events against the recorded monotonic microsecond timeline.
- Keeps P1/P2 and button identity.
- Releases any button still held when playback ends or is stopped.
- Playback begins on the first resumed gameplay update, rather than while the pause menu is still open.
- The pause menu has a PLAY LATEST MACRO control.

This is an initial playback implementation, not a verified frame-perfect player. Events are dispatched from the gameplay update hook, so actual delivery precision is bounded by the game's update cadence and must be measured in Geometry Dash on each target platform. Seeking, speed control, pause/resume, manual macro selection, compatibility fingerprints and in-game validation remain outstanding.

For the first test, record a short macro, return to the same level, pause immediately at the start, and choose PLAY LATEST MACRO. A replay from a different level state, checkpoint, speed or mode is not expected to match.

### Frame Counter — first in-game display
- Shows REC or PLAY state.
- Displays elapsed time in milliseconds.
- Displays a normalized elapsed frame count using a fixed 240 FPS reference.
- The normalized value is derived from the monotonic timestamp, not the monitor refresh rate or the game's command index.

The normalized frame is a time-analysis unit only. It does not mean the game simulation is running at 240 FPS. A full timeline, event-to-event deltas, frame stepping, filters and analysis controls are still planned.

### Hit Borderless — live prototype
- Thin yellow P1/P2 trajectory strokes.
- Dark-yellow Press markers and white Release markers.
- Live drawing is limited to a recent sample window to reduce mobile overhead.

World-object collision lines, solid surfaces, orbs, collision geometry and physics-based survivability analysis are not implemented. The current overlay still needs visual and performance testing in Geometry Dash.

## Timing model

Macro Engine preserves separate references:

- frame: GJGameState::m_commandIndex, captured from the game.
- update_steps: number of PlayLayer update callbacks observed during recording.
- timestamp_us: monotonic elapsed microseconds.
- normalized_frame_240: floor(timestamp_us * 240 / 1,000,000).

These values are not interchangeable. The normalized frame is useful for a consistent display and analysis scale, while the original timestamp and game frame remain available for accurate interpretation.

## File format

Current native files use format "macro-engine" and schema_version 2. Each input stores its original frame, timestamp, normalized frame, button, action, player and sampled position. The trajectory array stores position samples separately.

The current loader intentionally accepts schema 2 only, limits files to 64 MB and input lists to one million events, and rejects malformed or non-chronological input sequences.

## Development roadmap

1. Test recorder, save/load and playback in Geometry Dash on Android and Windows.
2. Measure playback timing error and improve scheduling where the game API permits.
3. Add macro library UI with manual selection, rename and delete.
4. Expand Frame Counter with event deltas, input markers and timeline controls.
5. Add seeking, pause/resume, speed control and playback compatibility checks.
6. Add Practice Mode checkpoint metadata and segment-aware playback.
7. Add import/export adapters after inspecting real sample files.
8. Build event editing and analysis.
9. Complete Hit Borderless object geometry and survivability analysis.
10. Add Layout Mode and Showcase Recorder.

## Build

Install the Geode SDK and CLI, then configure GEODE_SDK to point to the SDK directory.

```sh
geode build
```

The project currently targets Geometry Dash 2.2081 and Geode 5.11.2.
