# Startup Game Selector Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Zelda-themed game selector before the existing language screen, with *Parallel Worlds* visible but unavailable until its native port is ready.

**Architecture:** Persist the game choice in the existing runtime settings, independently from the language. Initialize SDL and the renderer before loading game assets, draw the selector with the existing software pixel primitives and static bitmap font, then load the currently supported game and continue through the existing language flow.

**Tech Stack:** C, SDL2, existing native pixel renderer and INI configuration.

**Spec:** `docs/superpowers/specs/2026-10-23-startup-game-selector-design.md`

## Global Constraints

- Use the two cover images supplied by the project owner from `assets/startup_covers/`; keep them sharp and show them as cardboard boxes in a centered, All-Stars-inspired carousel.
- Show *Parallel Worlds* with the exact status `PORT EN COURS`; it cannot start until a separate native port adds its assets and behavior.
- Keep the game preference separate from `Language` and do not add dependencies.
- Preserve keyboard, controller, mouse, and window-close behavior.

## Review Focus

- Old or missing `Game` settings must start *A Link to the Past* safely.
- Invalid or unsupported `Game` values must not select an unplayable title.
- Confirming the disabled *Parallel Worlds* case must not load the base game under a false title.
- Mouse selection must still work with aspect-ratio letterboxing and integer scaling.
- Closing during either startup screen must release only initialized SDL/renderer resources.

---

## File Map

- `src/config.h` — add the game preference to `Config`.
- `src/config.c` — default, parse, validate, and persist the `Game` setting.
- `src/main.c` — draw the two-case menu, handle selection, and move startup initialization so it precedes game asset loading.

The project has no separate unit-test suite. The tasks use the existing Windows TCC build and focused manual startup checks; no test framework or dependency is added.

### Task 1: Persist the game preference

**Files:**
- Modify: `src/config.h`
- Modify: `src/config.c`

**Interfaces:**
- Add `const char *game` to `Config`.
- Supported values are `alttp` and `parallel-worlds`; default to `alttp`.

- [x] **Step 1: Add the default and parser handling**

  Initialize `g_config.game` to `"alttp"` in `ParseConfigFile()`. In the `[General]` handler, parse `Game` case-insensitively, accept only the two supported values, and leave the default intact for an invalid value.

- [x] **Step 2: Persist the game choice**

  Write `Game = <value>` beside `Language` in the runtime settings block written by `Config_SaveUserSettings()`. Keep language and game independent.

- [ ] **Step 3: Build and inspect configuration behavior**

  Build with the repository's Windows TCC command (`run_with_tcc.bat` or the equivalent command in `BUILDING.md`). Manually check that a missing or invalid `Game` falls back to `alttp`, and that both recognized values survive a save and reload without removing unrelated settings.

### Task 2: Add the case selector before game loading

**Files:**
- Modify: `src/main.c`

**Interfaces:**
- Add `static bool SelectStartupGame(SDL_Window *window)`; return `false` when startup is cancelled or the window closes.
- Add local draw helpers for the screen and each case; reuse `FillRectNative()`, `RenderTextAt()`, and `MapStartupMouseToScreen()`.

- [x] **Step 1: Draw the menu and box-art carousel**

  Add a responsive 16:9-friendly screen with the supplied cover images displayed as cardboard cases. Enlarge the selected case at center, offset the other case to the side, and add a bright gold selection border, title, status, and control hints. Use uppercase ASCII labels supported by `RenderTextAt()`. Show `PORT EN COURS` over *Parallel Worlds*; keep its selection disabled.

- [x] **Step 2: Handle input and persist an available choice**

  Support left/right and up/down keyboard navigation, controller D-pad navigation, mouse hover and click, and Return/Space or controller A/Start to confirm. The *Parallel Worlds* card may be highlighted for inspection but confirmation must leave the user on this screen. Confirming *A Link to the Past* updates `g_config.game`, saves it only when changed, and returns `true`.

- [x] **Step 3: Reorder startup around the selector**

  Keep config parsing and display-size calculation first. Initialize SDL, window, and renderer; call `SelectStartupGame()`; then run the existing `LoadAssets()`, `LoadLinkGraphics()`, `ZeldaInitialize()`, and HD-pack initialization in their existing relative order. Call `SelectStartupLanguage()` after game initialization as it does today. Add an early-cancel cleanup path that destroys only SDL/renderer/window resources initialized before asset loading.

- [ ] **Step 4: Build and manually verify the complete flow**

  Build with the Windows TCC command. Launch and check that the game selector appears first, the selected case is clear, *Parallel Worlds* cannot start, confirmation proceeds to the language screen, and language choice still saves. Check keyboard, controller if connected, mouse, window close, and one window size with letterboxing.

## Completion Check

Confirm the startup sequence still initializes all game, audio, renderer, and texture-pack systems before gameplay, and that the user settings file retains unrelated settings after saving either choice.
