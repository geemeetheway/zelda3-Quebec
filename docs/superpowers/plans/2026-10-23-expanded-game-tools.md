# Expanded Game Tools Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. Track completed tasks with checkboxes.

**Goal:** Deliver backlog items 25–34 as integrated game tools while preserving the crisp pixel presentation.

**Architecture:** Extend the existing SDL graphics menu and renderer, adding focused local helpers instead of another UI framework. Reuse `src/config.[ch]`, `src/zelda_rtl.[ch]`, the PPU's safe extra-space bounds, and the existing image decoder where practical. Store all new user data under `hd_textures/`, `saves/`, and `user_data/`.

**Tech Stack:** C, SDL2, the existing TCC build, existing PPU and save-state recorder, standalone HTML/CSS/JavaScript.

**Spec:** `docs/superpowers/specs/2026-10-23-expansion-pack-design.md`

## Global Constraints

- Preserve all prior working-tree changes and root `AGENTS.md`.
- Keep pixel scaling nearest-neighbor; no full-screen CRT, NTSC, or blur.
- Treat atlas, challenge, and save labels as local metadata; do not write ROM data.
- Do not add or run tests; compile the executable and run `git diff --check`.

## Review Focus

- Malformed or missing image-pack manifests must not crash startup or change the active pack.
- Atlas persistence must not reveal unvisited screens or corrupt on a partial write.
- Paused photo and frame-step actions must not leak inputs into gameplay.
- Save-history copies must precede overwrite and remain bounded to three versions.
- Boss checkpoint restore must run under the audio/state mutex and refresh the paused frame.

---

### Task 1: HD asset pack discovery and manager

**Files:** `src/main.c`, `src/config.[ch]`, `src/hd_texture_pack.[ch]` (new), `hd_textures/README.md` (new).

- [x] Add pack discovery under `hd_textures/<pack>/manifest.ini`, active-pack persistence by folder name, preview/description fields, and empty/invalid pack handling.
- [x] Define nearest-neighbor replacement asset dimensions and manifest examples; do not bundle replacement art.
- [x] Add a manager page to the in-game menu and report the active pack.

### Task 2: Photo mode and bounded camera framing

**Files:** `src/main.c`, `src/zelda_rtl.c`, `snes/ppu.[ch]`.

- [x] Add a paused photo page with HUD/menu visibility, integer zoom, and pan controls over the existing rendered frame.
- [x] Add a configurable camera bias clamped to available world/room side space; persist it and expose a reset action.
- [x] Route screenshot capture from photo mode through the existing BMP writer.

### Task 3: Explored-area atlas

**Files:** `src/main.c`, `src/extra_tools.[ch]` (new), `.gitignore`.

- [x] Record visited overworld screens by world and screen index; save a versioned local bitset atomically.
- [x] Draw a two-world grid overlay containing only visited screens and the current position.
- [x] Add clear-atlas confirmation and a guide section.

### Task 4: Replay ghost and boss lab

**Files:** `src/main.c`, `src/zelda_rtl.[ch]`, `readmd.html`.

- [x] Keep a bounded ring of recent Link positions and add a toggleable translucent trail/ghost overlay.
- [x] Add explicit save/restore APIs for `saves/boss-lab.sav` using existing state serialization and audio locking.
- [x] Add menu controls and status messages; invalidate cached paused frames after restore.

### Task 5: Accessibility profiles

**Files:** `src/config.[ch]`, `src/main.c`, `readmd.html`.

- [x] Add Standard, High Contrast UI, and Low Flash profiles; map Low Flash to the existing dim-flash feature.
- [x] Apply profile colors to overlays and preserve all profiles in `zelda3.user.ini`.

### Task 6: Frame-pacing graph

**Files:** `src/main.c`, `readmd.html`.

- [x] Record a 120-sample frame interval history and add graph mode to the existing performance cycle.
- [x] Draw scaled bars within the current frame and label average, maximum, and target frame time.

### Task 7: Save labels and bounded history

**Files:** `src/main.c`, `src/save_metadata.[ch]` (new), `.gitignore`, `readmd.html`.

- [x] Add editable labels per slot and display them in the save manager.
- [x] Before overwrite, rotate the previous save into `.bak1`–`.bak3`; display the newest backup date and offer restore.
- [x] Write metadata atomically and handle absent/corrupt metadata with default slot names.

### Task 8: Local challenge tracking

**Files:** `src/main.c`, `src/extra_tools.[ch]`, `readmd.html`.

- [x] Track play time, deaths, visited screens, and peak rupees for the current local run.
- [x] Add optional goals for a time limit and no-death run; store personal records separately from save states.
- [x] Add reset confirmation and a compact challenge status page.

### Task 9: Documentation and final verification

**Files:** `readmd.html`, `docs/superpowers/specs/2026-10-23-expansion-pack-design.md`.

- [x] Document the pack layout, photo controls, atlas, ghost, boss lab, accessibility, graph, save history, and challenge page in French and English.
- [x] Build with the approved TCC command, run `git diff --check`, inspect changed files, and record any gameplay-only limitations.
