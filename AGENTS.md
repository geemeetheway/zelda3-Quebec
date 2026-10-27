# Consignes communes du dépôt

## Convention GitHub — PR purement documentaires (2026-10-27)

- **Ne plus demander une instruction ponctuelle pour éviter la CI** des PR
  qui modifient exclusivement de la documentation sans effet exécutable.
  Si une CI GitHub Actions existe, elle doit ignorer ces changements sur
  les événements `pull_request` **et** `push` après fusion.
- Classer les **fichiers réellement modifiés du diff complet** : documents
  Markdown racine et documentation textuelle sous `docs/` sont admissibles
  lorsqu'ils ne changent aucun contrat exécuté. La politique du workflow
  utilise une liste sûre explicite, jamais la description de la PR.
- Une modification de code, tests, scripts, dépendances, workflow
  `.github/workflows/`, configuration exécutable ou contenu ambigu entraîne
  les contrôles CI habituels. Pour une PR mixte, ne jamais utiliser
  `[skip ci]` pour masquer ces contrôles.
- Relire les documents et exécuter localement les vérifications Markdown
  disponibles ; ne pas annoncer une validation qui n'a pas eu lieu.
- Respecter les protections de branche et les règles d'approbation :
  **l'exemption de CI n'autorise pas à elle seule une fusion**.
- En l'absence de CI, conserver cette convention sans créer de workflow
  inutile. Configurer le filtrage lorsqu'une CI sera ajoutée.
