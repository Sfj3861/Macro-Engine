# Macro Engine

A Geode toolkit for Geometry Dash focused on replay analysis, frame timing and player trajectory visualization.

## Current tools

### Frame Counter (.fcr)
- Import a Geometry Dash replay in `.gdr`, `.gdr.json` or `.gdr2` format from the pause menu.
- Reads input frames, press/release state, button and player identity.
- Keeps the replay framerate, level metadata and duration.
- Exports a native `.fcr` analysis file to the mod's `analyses/` save directory.
- Displays the live game frame beside the imported replay's next-input distance.

### Hit Border (.hbr)
- Imports the same three replay formats.
- Exports a native `.hbr` file containing replay inputs and any available trajectory samples.
- Draws P1 and P2 paths in the level when position data is available.
- Supports the standard GDR2 `Phys` input extension (position and rotation fields).
- Can also read common position keys from legacy GDR JSON/MessagePack input extensions.

**Trajectory limitation:** standard GDR and GDR2 files usually contain input events and frames, not player coordinates. Hit Border will still create an HBR analysis from those files, but it cannot draw a real trajectory unless the replay includes recognized position data. Macro Engine does not invent player positions or collision geometry.

## Supported replay formats

- **GDR v1 binary (`.gdr`)**: MessagePack-encoded replay data.
- **GDR v1 JSON (`.gdr.json`)**: JSON-encoded replay data.
- **GDR2 (`.gdr2`)**: compact binary format with a GDR header, variable-length integers and delta-encoded frames.

GDR2 parsing uses the upstream GDReplayFormat library. Legacy GDR uses the documented v1 data structure and MessagePack decoding.

## Native analysis formats

Both native files are JSON documents with a versioned schema:

- `.fcr`: frame counter metadata and the ordered input timeline.
- `.hbr`: replay metadata, input timeline and trajectory samples when present.

Files are saved under the Geode mod save directory in `analyses/`. The original replay is never modified.

## Important notes

- Frame numbers are read directly from the replay. They are not inferred from monitor refresh rate.
- The replay's framerate is retained as metadata; it is not assumed to be 240 FPS.
- The live Frame Counter HUD reads the current Geometry Dash game frame and shows the distance to the next imported input.
- Hit Border currently visualizes imported position samples. It does not yet calculate hitboxes, object collision geometry, orb collision zones or physics-based survivability.
- Import and export have size and event-count limits to avoid loading unexpectedly large files.

## Build

Install the Geode SDK and CLI, then configure `GEODE_SDK` to point to the SDK directory.

```sh
geode build
```

The project targets Geometry Dash 2.2081 and Geode 5.11.2.
