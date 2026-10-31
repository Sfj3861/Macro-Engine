# Hit Borderless

A Geode toolkit for Geometry Dash, starting with **Macro Engine**: reliable input recording and a native macro format that will power later analysis tools.

## Current development status

### Implemented in the initial source scaffold
- Pause-menu control to start a macro recording.
- Capture of gameplay press/release events.
- Separate P1/P2 event field.
- Frame counter driven by `PlayLayer::postUpdate`.
- Monotonic microsecond timestamp for each event.
- Automatic JSON-based `.hbmacro` save when stopping or leaving a level.

### Planned next
1. Macro library browser and loader.
2. Macro playback with compatibility checks.
3. Import/export adapters for `.grd`, `.gdr.json`, and `.gdr2`.
4. Macro timeline editor.
5. Frame Window Analyzer and Hit Borderless hitbox visualization.
6. Layout Mode and Showcase tools.

## Build

Install the Geode SDK and CLI, then configure `GEODE_SDK` to point to your SDK directory.

```sh
geode build
```

The current project targets Geometry Dash 2.2081 and Geode 5.11.2.

## Recording a macro

1. Enter a level.
2. Open the pause menu.
3. Select **START MACRO RECORDING**.
4. Resume gameplay.
5. Pause and select **STOP & SAVE MACRO**, or leave the level to autosave.
6. Find the resulting `.hbmacro` file in the mod's save directory under `macros/`.

The first milestone records input transitions. Playback, external macro parsers, and collision analysis are not implemented yet.

## Important timing note

The recorder stores both a PlayLayer update-frame index and a monotonic microsecond timestamp. These are different clocks and must not be treated as interchangeable. The initial frame counter is an update-step counter, not a claim that every device is running at a fixed refresh rate.
