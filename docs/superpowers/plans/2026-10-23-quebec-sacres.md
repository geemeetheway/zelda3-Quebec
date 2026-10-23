# Québécois With Sacres Implementation Plan

> **For agentic workers:** Implement inline in the current checkout. Preserve unrelated changes and root `AGENTS.md`; do not commit.

**Goal:** Add an independent Quebec French locale that uses sacres as interjections, nouns, qualifiers, and conjugated verbs.

**Architecture:** Register `fr-ca-sacres` in the existing resource pipeline, reuse the French font, and store 397 adapted messages separately. Extend the startup chooser, validator, preview, build helper, and bilingual guide to five locales.

**Tech Stack:** C startup language selector; Python resource, preview, and validation tools; UTF-8 dialogue source.

**Spec:** `docs/superpowers/specs/2026-10-23-quebec-sacres-design.md`

## Global Constraints

- Preserve all 397 message IDs and ordered bracket-command tags.
- Use only characters in the existing French alphabet.
- Keep the four current locale sources unchanged.
- Reuse current build and validation tools; add no separate test suite.

## Review Focus

- Sacre used with the wrong grammatical role — review selected dialogue examples for interjection, noun, qualifier, and inflected verb usage.
- An unsupported glyph or overlong line — run the locale validator with the French font.
- A changed game command or omitted message — compare IDs and ordered tags with the Quebec source.
- A selector count mismatch — compile the executable after adding the fifth button.

---

### Task 1: Register the locale

**Files:** `assets/text_compression.py`, `assets/sprite_sheets.py`, `assets/compile_resources.py`, `assets/restool.py`, `assets/build_localized.py`

- [x] Register `fr-ca-sacres` with French text data and font fallback; route its source from `localization/dialogue_fr_ca_sacres.txt`.
- [x] Require the source in the localized builder and include the locale in the tool's language list.

### Task 2: Adapt dialogue

**Files:** Create `localization/dialogue_fr_ca_sacres.txt`

- [x] Copy the familiar Quebec source, then revise selected lines with contextual exclamations, nominal use, qualifier phrases, and conjugated forms such as `s'en câlisser`, `sacrer le camp`, and `crisser`.
- [x] Leave solemn dialogue comparatively restrained and keep directions unambiguous.

### Task 3: Expose and document the choice

**Files:** `src/main.c`, `assets/validate_localization.py`, `assets/preview_localization.py`, `localization/README.md`, `readmd.html`

- [x] Add the fifth startup option and persisted code.
- [x] Add the fifth source and column to validation and preview; update player-facing documentation.

### Task 4: Verify

- [x] Run the localization validator and preview generator.
- [x] Build the five-locale resource bundle and Windows executable.
- [x] Run the CRLF-aware diff check and confirm the plan has no open tasks.
