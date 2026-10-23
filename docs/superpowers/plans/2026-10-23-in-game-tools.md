# In-Game Tools Implementation Plan

> **For agentic workers:** Implement the tasks below in order in the current workspace. Existing uncommitted changes must be preserved.

**Goal:** Add persistent graphics settings, save management, screenshots, input remapping, render diagnostics, a configurable HUD, frame stepping, and a bilingual interactive manual.

**Architecture:** Keep emulator state and frame execution in the existing C runtime. Extend the current SDL graphics menu with focused pages; save user choices and control mappings to `zelda3.user.ini`; write screenshots and save previews as BMP files without external libraries. Keep the manual self-contained in root `readmd.html` with an English/French toggle.

**Tech Stack:** C, SDL2, existing INI parser, standalone HTML/CSS/JavaScript.

**Spec:** User request in this conversation: deliver items 17–24, with the manual named `readmd.html` in English and French.

## Constraints and Review Focus

- Preserve existing repository modifications; follow the root `AGENTS.md` style.
- Do not add or run tests; compile with the already configured Windows TCC command and run `git diff --check`.
- Keep settings local to `zelda3.user.ini`; do not overwrite `zelda3.ini`.
- Ensure controller/keyboard capture never leaks inputs into gameplay.
- Ensure save previews, screenshots, and preview loads handle missing files cleanly.
- Ensure frame advance executes exactly one game frame and remains paused.
- Ensure the user manual content and controls stay synchronized with the runtime.

## Tasks

### 1. Config persistence and live control remapping [x]

Extend `src/config.[ch]` to replace existing mappings for each command, track keyboard and controller mappings, format them for display, and atomically save graphics/HUD options plus mappings to `zelda3.user.ini` while including `zelda3.ini` as the base.

### 2. Graphics menu pages and HUD options [x]

Extend `src/main.c` menu navigation with graphics, HUD, save, and controls pages. Add toggles for health, magic, rubies, keys, and HUD placement. Persist changes through the config API; add keyboard/controller capture and cancellation.

### 3. Save manager and preview images [x]

List slots 1–11 with modified timestamps and load/save actions. Confirm overwrites. Store an 80×45 BMP preview beside each save and show the selected preview in the manager.

### 4. Screenshot capture [x]

Add a Print Screen command that saves the current rendered frame to a uniquely named BMP in `screenshots/`.

### 5. Render diagnostics and frame advance [x]

Cycle the performance display through off, FPS, and detailed modes. Report frame time, output mode, dimensions, and Mode 7 GPU availability. Add a paused single-frame advance command with keyboard and controller defaults.

### 6. Bilingual interactive manual [x]

Create `readmd.html` with French/English switching, searchable sections, and instructions for current controls, graphics options, saves, screenshots, remapping, statistics, and frame stepping.

### 7. Verification [x]

Build `zelda3.exe` with TCC, inspect the generated artifact, run `git diff --check`, and review the final diff for integration mistakes. No gameplay visual test is planned in this pass.
