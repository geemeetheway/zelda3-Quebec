# Expanded Game Tools Design

## Intent

Deliver backlog items 25–34 as a second set of local, in-game tools for the Zelda3 fork. The owner wants to push the existing widescreen presentation and make the game easier to explore, capture, practice, and tune while keeping its pixel art crisp.

## Goals

- Add a discoverable HD asset-pack manager with a documented drop-in format and nearest-neighbor rendering. No third-party dependency or global image filter.
- Add a paused photo mode, persistent explored-area atlas, Link replay ghost, and boss checkpoint workflow.
- Improve scene framing within the PPU's existing room/world bounds.
- Add accessible display/input profiles, a frame-pacing graph, named save slots with a short backup history, and local challenge statistics.
- Allow independent time-limit and no-death challenge goals, and provide a camera-focus reset action.
- Persist tool preferences and metadata locally; keep ROM-derived/game save content untouched except when the player explicitly saves or restores a checkpoint.
- Update the offline French/English `readmd.html` guide.

## Non-goals and constraints

- Do not bundle generated replacement art or claim that external HD art exists. The manager must explain the pack format and safely report an empty pack folder.
- Do not use CRT, NTSC, blur, or other full-screen filtering.
- Do not expose unexplored rooms in the atlas or modify normal game progression for challenges.
- Keep generated screenshots, maps, labels, and backup files outside the tracked ROM/assets.
- Preserve the existing working tree, configuration include flow, SDL2/TCC build, and two-space C style. Do not add or run tests; verify by compiling and reviewing the diff.

## Approaches considered

1. **Native C/SDL tools integrated into the current menu (selected).** Reuses SDL input, the PPU, the existing save-state recorder, and the current local configuration. It keeps one executable and matches the existing interaction model. The HD manager will be a usable local pack manager with a documented asset manifest; replacement art remains user supplied.
2. External companion utility. This avoids changing emulator internals but cannot reliably control paused frames, PPU bounds, or boss checkpoints.
3. Broad shader-only implementation. This is quick for image effects but cannot manage saves, input, maps, or replay state and conflicts with the requested crisp image.

## Design decisions

- Add a small tool page to the existing paused menu rather than a second UI framework.
- Store pack selection by stable folder name, accessibility profile, challenge options, save labels, atlas visits, and challenge records in local user files.
- Record only safe room/screen identifiers and Link positions for the atlas and ghost. Keep challenge tracking as local counters.
- Keep camera adjustment clamped to the extra side space already calculated from world and room bounds.
- Back up an occupied save before overwrite, retaining three prior copies.
- Use an explicit user action to create and restore the boss-lab checkpoint.

## Acceptance

All ten menu features can be reached and used without an external service; each generated file has a stable location and missing optional data degrades cleanly. The TCC executable builds and `git diff --check` is clean. Gameplay visual behavior still needs a hands-on playthrough after this implementation.
