# Beauce Slang Locale Implementation Plan

> Implement inline in the existing checkout, preserving all unrelated working-tree changes and the root `AGENTS.md`.

**Goal:** Add an additional, Beauce-flavored slang French locale without replacing the existing Quebec French adaptation.

**Spec:** `docs/superpowers/specs/2026-10-23-beauce-slang-design.md`

## Constraints

- Use France French as the source; no additional ROM is needed.
- Keep message IDs and the exact ordered bracket-command sequence.
- Keep proper names, item names, and gameplay instructions clear.
- Use the French font/alphabet; no accented character outside that font.
- Do not add or run a separate test suite. Run the localization validator and build the resources and executable.
- Do not commit; the working tree has unrelated pre-existing changes.

## Tasks

### 1. Add locale to resource tooling

- [x] Register `fr-beauce` with the French dictionary/alphabet and shared font fallback.
- [x] Route its dialogue source from `localization/` and add it to the localized build helper.

### 2. Adapt the dialogue

- [x] Create 397 Beauce-flavored slang messages in `localization/dialogue_fr_beauce.txt`.
- [x] Use contextual regional turns and oral contractions, keeping the version readable and non-caricatural.
- [x] Preserve proper names, item/place names, message IDs, and exact ordered game tags.

### 3. Extend review and validation

- [x] Add the fourth column to the HTML preview and the fourth source to validation.
- [x] Check IDs, ordered tags, French glyphs, and estimated line widths.

### 4. Add player-facing selection and docs

- [x] Add the fourth option to the startup language selector and persist the locale as `fr-beauce`.
- [x] Update the French/English guide and localization README.

### 5. Verify

- [x] Run the localization validator and preview generator.
- [x] Build the four-locale resource bundle and Windows executable.
- [x] Run the CRLF-aware diff check and preserve uncommitted unrelated changes.
