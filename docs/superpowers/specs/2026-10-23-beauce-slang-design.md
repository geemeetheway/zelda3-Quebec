# Beauce-flavored Quebec Slang Localization

## Intent

Add a fourth startup language, **Français beauceron (slang)**, while keeping the existing France French and Quebec French editions unchanged.

## Goals

- Maintain a separate `localization/dialogue_fr_beauce.txt` source for all 397 messages.
- Adapt the already familiar Quebec version with more regional and oral turns that remain understandable in context.
- Keep item names, places, character names, game commands, gameplay directions, and message identifiers intact.
- Use only the existing French glyph set and check dialogue width using the extracted French font.
- Include the locale in compilation, the startup selector, the side-by-side preview, and the validator.

## Style

Use Beauce-inspired regionalisms where they fit the speaker and scene; do not apply phonetic spelling across the game or turn every line into a joke. Keep the slang clear enough for other Quebec players. Use mild swearing only where the original character's tone supports it. Preserve the current Quebec edition as a less region-specific alternative.

The regional word list is informed by Frederic Desjardins, “Des mots qui ajoutent de la couleur au parler beauceron,” *L'Éclaireur Progrès* (18 May 2016): https://www.leclaireurprogres.ca/dossiers/des-mots-qui-ajoutent-de-la-couleur-au-parler-beauceron/ . Terms are selected by context; the list is illustrative, not a claim that every Beauceron uses the same vocabulary.

## Architecture

Register locale code `fr-beauce` with the existing French alphabet and dictionary, and reuse the extracted France French font. Store the adapted text separately, include it in the normal resource bundle, add a fourth SDL startup option, and expand the offline review and validation tools to four locales.

## Acceptance

All four language sources have the same sequential IDs. The new dialogue matches the France source's ordered command tags, uses supported glyphs, and produces no estimated line-width warnings. The localized resource bundle and executable compile successfully.
