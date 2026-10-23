# French and Quebec French Localization Design

## Intent

Use the available France French ROM as the source for a separate, very familiar Quebec French game-text edition. Let players choose English, France French, or Quebec French at every launch, defaulting to their last selection.

## Goals

- Automate French text extraction and rebuilding of the multilingual asset bundle.
- Keep Quebec dialogue in `localization/dialogue_fr_c.txt`, based on the extracted French text.
- Preserve game commands and message identifiers while adapting wording into very familiar Quebec French.
- Provide a searchable side-by-side dialogue preview and a localization validator for identifiers, command tokens, supported characters, and line-width warnings.
- Show a startup language selector and persist its choice in `zelda3.user.ini`.

## Non-goals and constraints

- Do not require or assume a Quebec ROM; the France ROM is the translation base.
- Do not modify or redistribute either ROM.
- Keep the US ROM as the source of gameplay, maps, graphics, and non-dialogue resources.
- Keep the launcher prompt native to SDL, block gameplay until a choice is made, and use the previous locale as the default action.
- Preserve dialogue IDs and all bracketed game control commands. Flag unsupported text instead of silently replacing it.

## Approaches

1. **Adapt the extracted France dialogue into a separate `fr-c` source (selected).** The France ROM is present, the resource tools already define `fr-c`, and the runtime already loads language blocks. This preserves the English and France French versions while allowing a Quebec adaptation without a Canadian ROM.
2. Wait for an official Canadian French ROM. This is not available locally and is unnecessary for the requested adaptation.
3. Translate at runtime from France French. This would add runtime dependencies, make message encoding and layout less predictable, and prevent the existing asset compiler from validating the result.

## Design decisions

- Store the Quebec source as `localization/dialogue_fr_c.txt`; generate the France source and font from the supplied France ROM on demand.
- Teach the resource compiler to read the Quebec source directly, and reuse the extracted France font for `fr-c` unless a local Quebec font is later added. The build helper must never overwrite the Quebec source.
- Generate an offline HTML review page from the three dialogue files. Include message IDs, search, side-by-side text, and length/validation indicators.
- Validate message IDs, bracketed control tokens, accepted French glyphs, and conservative text-width limits before compiling.
- The launch chooser offers `English`, `Français (France)`, and `Français québécois`; selection is applied before the first game frame and saved as `us`, `fr`, or `fr-c`.

## Acceptance

The language build helper can regenerate a multilingual `zelda3_assets.dat` from the root US ROM and local France ROM without overwriting Quebec dialogue. The preview and validator can open the generated source files. Startup selection persists and resolves to the correct compiled language block. Compile the game and run `git diff --check`; do not add or run a separate test suite.
