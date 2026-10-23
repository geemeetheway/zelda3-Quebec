# Recommandations de publication et de diffusion

Ce document résume les précautions recommandées avant de promouvoir publiquement le projet, notamment auprès de médias québécois, de créateurs de contenu et de communautés rétro.

> Ce document présente des recommandations pratiques de gestion du risque. Il ne constitue pas un avis juridique.

## Objectif

Le projet peut bénéficier d'une forte visibilité grâce à son angle québécois, à ses variantes de dialogues et à ses améliorations techniques. Cette visibilité augmente toutefois aussi la probabilité que Nintendo ou un ayant droit remarque le dépôt.

L'objectif est donc de maximiser la visibilité du travail original tout en évitant de distribuer du contenu Nintendo qui n'est pas nécessaire au projet.

## Modèle de diffusion recommandé

Privilégier une distribution sous forme de code source, de scripts, de fichiers de traduction et de patchs.

Ne pas distribuer :

- de ROM SNES complète;
- de ROM prépatchée;
- de copie du jeu original;
- de fichiers extraits directement de la ROM lorsqu'ils peuvent être régénérés par l'utilisateur;
- de clés, de moyens de contournement ou d'outils destinés à contourner une protection technique.

L'utilisateur devrait fournir lui-même une copie compatible du jeu original, puis utiliser les outils du projet pour générer les ressources nécessaires.

Pour une future traduction de *Parallel Worlds*, le même principe est recommandé : publier le patch ou les données de traduction, jamais une ROM déjà modifiée.

## Dépôt GitHub

Avant une campagne médiatique, le dépôt devrait permettre à un visiteur de comprendre le projet en quelques secondes.

Recommandations :

1. Mettre la branche contenant le travail québécois en évidence.
2. Ajouter un README spécifique au projet et à ses variantes.
3. Expliquer clairement qu'aucune ROM Nintendo n'est distribuée.
4. Indiquer que l'utilisateur doit fournir sa propre copie compatible du jeu.
5. Conserver les ROM, les sauvegardes personnelles et les fichiers générés dans le fichier `.gitignore`.
6. Éviter de committer des ressources extraites du jeu lorsqu'elles peuvent être générées localement.
7. Conserver une séparation claire entre le code original du projet, les adaptations québécoises et le contenu provenant du jeu original.

## Présentation publique

L'angle médiatique recommandé est le travail d'adaptation et d'ingénierie, plutôt que la distribution d'un jeu Nintendo.

Exemples d'angles pertinents :

- adaptation culturelle québécoise;
- variantes de dialogues familier, Beauce et sacres;
- outils de validation et de comparaison des dialogues;
- améliorations graphiques et ergonomiques;
- travail de rétro-ingénierie et d'outillage autour d'un projet open source existant.

Éviter une formulation qui pourrait laisser croire que le dépôt distribue une version gratuite de *The Legend of Zelda: A Link to the Past*.

## Vidéos, captures et médias

Une courte vidéo est probablement le meilleur outil de promotion.

Elle peut montrer :

- un dialogue français de référence;
- la variante québécoise familière;
- la variante Beauce;
- la variante avec sacres;
- quelques améliorations visuelles ou fonctionnelles.

La communication devrait préciser que le projet est une adaptation non officielle et qu'il n'est ni affilié à Nintendo ni approuvé par Nintendo.

## Monétisation

Éviter de vendre le jeu, une ROM, une cartouche prépatchée ou un accès payant au contenu adapté.

Si le projet devient très visible, toute forme de monétisation devrait être évaluée séparément avec un professionnel du droit de la propriété intellectuelle.

## Si Nintendo ou GitHub envoie un avis

Ne pas ignorer l'avis.

Réaction recommandée :

1. Conserver une copie complète de la demande.
2. Identifier exactement les fichiers ou éléments visés.
3. Suspendre la redistribution des éléments contestés.
4. Ne pas republier immédiatement le même contenu ailleurs.
5. Ne pas envoyer une contre-notification DMCA sans comprendre ses conséquences.
6. Consulter un avocat canadien spécialisé en propriété intellectuelle avant de contester formellement une demande importante.

Une demande de retrait est généralement un risque plus immédiat qu'une poursuite, mais une poursuite reste juridiquement possible.

## Recommandation générale

Le modèle le plus prudent pour ce projet est :

**code et outils publics + traductions originales + patchs + utilisateur qui fournit sa propre copie du jeu.**

Ce modèle ne garantit pas l'absence de réclamation, mais il réduit l'exposition inutile et correspond à la façon dont de nombreux projets de ROM hacking sont distribués depuis longtemps.

## Avant une couverture médiatique

Avant de solliciter activement des médias, vérifier les points suivants :

- la bonne branche est clairement visible;
- le README explique le projet en moins d'une minute;
- aucune ROM n'est présente dans le dépôt;
- aucun fichier généré inutile n'est publié;
- une courte démonstration vidéo est prête;
- les crédits du projet d'origine sont clairs;
- la mention « adaptation non officielle » est visible;
- les instructions de construction demandent explicitement à l'utilisateur de fournir sa propre copie compatible du jeu.

Cette préparation augmente à la fois la qualité de la présentation publique et la capacité à répondre rapidement si le projet reçoit beaucoup d'attention.
