# Québécois With Sacres Localization

## Intent

Add a fifth startup language, **Québécois (sacres)**, for players who want a distinctly oral Quebec voice with frank profanity. Keep the four existing locales unchanged.

## Goals

- Add an independent 397-message dialogue source and make it selectable at launch.
- Use Quebec sacres naturally as interjections, nouns, qualifiers, and conjugated verbs. Use the grammar and role distinctions described by Usito's [article on Quebec sacres](https://usito.usherbrooke.ca/articles/th%C3%A9matiques/vincent_1).
- Preserve character and scene tone; keep serious story beats and gameplay instructions understandable.
- Preserve message IDs, ordered game-command tags, proper names, and supported French glyphs.
- Include the locale in the bundle builder, validator, and comparison preview.

## Approaches

1. **Curated fifth edition based on familiar Quebec French (chosen).** Copy its dialogue into a separate source and adapt selected messages with grammar-aware sacres. This preserves the current edition and avoids changing every line just to increase profanity.
2. **Global word substitutions.** Rejected because an interjection, a noun, a qualifier, and a verb need different syntax and conjugation.
3. **A new translation from France French.** Rejected because the existing Quebec version is the closest voice and supplies established names and terminology.

## Constraints and Non-goals

- Register `fr-ca-sacres`; reuse the French alphabet, dictionary, and font.
- Do not modify the English, France French, familiar Quebec, or Beauce sources.
- Do not add a profanity toggle or change game behavior beyond language selection.

## Acceptance

All five locale sources cover the same sequential IDs and the new source retains the Quebec source's ordered command tags. The validator reports supported glyphs and no estimated line-width warnings. The preview, resource bundle, and Windows executable include the fifth locale.
