# French and Quebec French Localization Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline. Track progress in `.superpowers/sdd/2026-10-23-localization-fr-ca/progress.md`.

**Goal:** Build the French and very familiar Quebec French dialogue resources, review and validate them, and let players choose a language at startup.

**Architecture:** Add a Python build helper around the repository's existing ROM-aware resource tools. Keep the Quebec text as a separate checked-in source, generate an offline HTML review page and validator from those files, and use an SDL startup dialog before gameplay to select one of the compiled language blocks.

**Tech Stack:** C, SDL2, Python 3, existing `restool.py`/`compile_resources.py`, HTML/CSS/JavaScript.

**Spec:** `docs/superpowers/specs/2026-10-23-localization-fr-ca-design.md`

## Global Constraints

- Use the current US ROM for game resources and the supplied France ROM only to extract French dialogue and font.
- Do not modify or redistribute either ROM.
- Preserve message identifiers, bracketed game commands, and existing US/France French resources.
- Keep the Quebec French source independently editable and never overwrite it during extraction.
- Use the established Python/C style; retain two-space indentation in C.
- Do not add or run a separate test suite; compile the executable and validate generated localization data.

## Review Focus

- Missing or unsupported ROM paths must fail with a useful message before generated output is replaced.
- Missing Quebec source or font data must not produce an English/French asset mislabeled as Quebec French.
- The preview and validator must handle dialogue rows containing `: ` inside the text by splitting only at the first separator.
- Quebec text must retain each source message ID and bracketed runtime command tokens.
- A missing compiled locale or canceled startup dialog must fall back to the last valid language without leaking input into gameplay.

---

### Task 1: Automated localized asset build (#38)

**Files:** Create `assets/build_localized.py` and `localization/README.md`; modify `.gitignore`, `assets/compile_resources.py`, and `assets/sprite_sheets.py`.

- [x] Accept explicit `--us-rom` and `--fr-rom` paths with defaults matching the repository files.
- [x] Preflight ROM identities and required source files before extraction or compilation; read `localization/dialogue_fr_c.txt` directly.
- [x] Use the extracted France font for `fr-c` when no Quebec-specific font exists.
- [x] Stop before compilation if the Quebec translation source is absent; never create a false Quebec build by copying France French as finished output.
- [x] Ignore ROMs under `roms/` and generated locale staging files.
- [x] Run the helper and confirm it produces the resource bundle without changing ROM files or the Quebec source.

### Task 2: Quebec French dialogue (#35)

**Files:** Create `localization/dialogue_fr_c.txt` from the extracted France French source.

- [x] Preserve every message number and the exact ordered bracketed control tokens.
- [x] Adapt dialogue into very familiar Quebec French while keeping character intent, gameplay instructions, and item/place names clear.
- [x] Retain original orthography for required game tokens and use supported accented characters.
- [x] Review the full source and retain proper names, item/place names, and game commands.

### Task 3: Dialogue preview (#42)

**Files:** Create `assets/preview_localization.py`; generate ignored `localization/dialogue-preview.html`.

- [x] Show US English, France French, and Quebec French side by side by message ID.
- [x] Add client-side search, locale visibility toggles, and filters for missing or unusually long text.
- [x] Escape source text before embedding it in HTML and preserve control tokens visibly.

### Task 4: Localization validation (#37)

**Files:** Create `assets/validate_localization.py`.

- [x] Check identical sequential message IDs and complete source coverage.
- [x] Check exact bracket-command sequence per message and report differences by ID.
- [x] Check all Quebec characters against the `fr-c` alphabet and estimate line widths using the extracted font metrics.
- [x] Run the validator and localized asset build; resolve all errors and warnings.

### Task 5: Startup language selector (#36)

**Files:** Modify `src/main.c`, `src/config.c`, and `readmd.html`.

- [x] Add a three-choice SDL message box with the previously selected locale as its default button.
- [x] Apply the selected `us`, `fr`, or `fr-c` language before the first emulated frame.
- [x] Persist the selection under `[General]` in `zelda3.user.ini`; preserve the user's existing config and use the prior language if the dialog fails or closes.
- [x] Update the bilingual guide and compile the executable plus resource bundle.
