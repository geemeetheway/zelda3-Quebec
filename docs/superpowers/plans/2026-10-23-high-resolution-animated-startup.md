# High-Resolution Animated Startup Implementation Plan

> **For agentic workers:** Execute this plan task by task using the native workflow. Steps use checkbox syntax for tracking.

**Goal:** Render the game and language selectors at desktop resolution with an illustrated Zelda-style introduction, animated reveals, and short sound effects.

**Architecture:** Keep the game renderer unchanged after startup. Temporarily resize the renderer's draw surface for both startup screens, render the background and menu layers at that size, then restore the original SNES dimensions. Use the existing SDL/OpenGL renderer and stb image loader; synthesize short menu sounds through SDL audio.

**Tech Stack:** C, SDL2, OpenGL, stb_image.

**Spec:** `docs/superpowers/specs/2026-10-23-high-resolution-animated-startup-design.md`

## Global Constraints

- Keep the in-game framebuffer, filters, and rendering unchanged after startup.
- Keep Parallel Worlds visible as `PORT EN COURS` and non-launchable.
- Respect `enable_audio`; add no dependency or music.
- Preserve keyboard, controller, and mouse navigation, including high-DPI coordinate mapping.

## Review Focus

- OpenGL and SDL backends must both draw the same startup output size.
- HiDPI mouse clicks must still hit the visible card or language row.
- Allocation or background-load failure must fall back to the current menu without crashing.
- Resizing the window during a menu must not leave stale dimensions or buffers.
- Disabled audio must not open a device; enabled audio must close its temporary device before game audio starts.

## Files

- `assets/startup_covers/startup_background.png`: original, text-free illustrated backdrop.
- `assets/startup_covers/README.md`: document the backdrop alongside the cover assets.
- `src/main.c`: temporary startup canvas, responsive drawing, animation timing, mouse mapping, and synthesized cues.
- `src/opengl.c`: no change expected; its draw buffer already grows to the requested dimensions.

---

### Task 1: Add the illustrated backdrop

**Interfaces:** `LoadStartupBackground()` loads an optional RGBA image from the startup-covers directory; startup rendering remains usable when loading fails.

- [x] Generate a wide Hyrule map and ornate stone-and-gold frame, with no text, logos, covers, or UI.
- [x] Save the selected PNG at `assets/startup_covers/startup_background.png` and note its role in the assets README.
- [x] Inspect the image for unwanted text, watermarks, and layout conflicts with the two cover cards.

### Task 2: Render startup at output resolution

**Files:** modify `src/main.c`.

**Interfaces:** Add helpers that obtain the current renderer output dimensions, temporarily prepare/restore the SDL texture and logical size, and map window mouse coordinates to startup-canvas coordinates. OpenGL uses its existing resizable CPU buffer.

- [x] Render both startup selectors against the temporary output dimensions, scaling layout from the existing 426×240 design grid.
- [x] Draw the background to fill the canvas while keeping cover art and pixel-font UI crisp.
- [x] Restore the normal renderer texture, logical size, and integer-scaling behavior before gameplay.
- [x] Preserve a safe native-resolution fallback if the temporary canvas cannot be prepared.

### Task 3: Animate the introduction and add cues

**Files:** modify `src/main.c`.

- [x] Fade in the backdrop, drop and glow the title, reveal the two covers in order, and pulse a gold selection halo.
- [x] Start menu input immediately; any input during the intro skips to the interactive state.
- [x] Synthesize short title, reveal, navigation, and confirm sounds through a temporary queued-audio device gated by `enable_audio`.
- [x] Close the temporary audio device before opening the game's audio device.

### Task 4: Build and inspect

- [x] Compile with the repository's bundled Windows TCC command.
- [ ] Capture and inspect the rendered startup canvas at desktop dimensions; confirm the game draw dimensions return to their original values after language selection. The original `zelda3.exe` instance remains open, so the preview build was not launched for this inspection.
- [x] Run `git -c core.whitespace=cr-at-eol diff --check` and review the final diff for unrelated changes.
