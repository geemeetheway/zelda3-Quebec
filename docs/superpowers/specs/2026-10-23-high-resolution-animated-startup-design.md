# Menu de démarrage haute résolution et animé

**État :** approuvée

## Intention

Faire du choix du jeu une séquence d’accueil riche, fidèle à la référence fournie, et rendue à la résolution réelle de la fenêtre ou de l’écran. Les jaquettes officielles déjà fournies restent des images séparées, nettes et non recolorées. Le rendu de jeu conserve sa résolution actuelle.

## Parcours visuel et sonore

1. Le décor illustré apparaît en fondu : carte nocturne d’Hyrule, cadre de pierre et ornements dorés. L’image de fond ne contient ni texte, ni jaquette, ni logo.
2. Le titre descend depuis le haut, s’immobilise et reçoit un halo doré.
3. La boîte *A Link to the Past* apparaît, puis celle de *Parallel Worlds*. Un halo lumineux mobile marque le jeu choisi; *Parallel Worlds* garde l’état `PORT EN COURS` et ne peut pas être lancé.
4. Des sons originaux courts accompagnent l’arrivée du titre, des boîtes, le déplacement du halo et la confirmation. Aucun thème musical n’est ajouté. Les effets suivent le réglage audio existant.

Le clavier, la manette et la souris gardent leur navigation actuelle. Toute entrée pendant l’introduction termine les animations et rend immédiatement le menu actif.

## Approche retenue

Créer une toile temporaire à la taille de la surface affichée dans la fenêtre existante. OpenGL sait déjà redimensionner son tampon; le chemin SDL devra créer une texture temporaire haute résolution et désactiver sa taille logique pendant les écrans de démarrage. Après le choix du jeu et de la langue, le rendu revient au tampon natif du jeu. L’image illustrée est générée spécialement pour le projet; les jaquettes, le titre, les textes, les lumières et les animations restent des calques séparés.

L’alternative consistait à construire tout le décor en rectangles et primitives C. Elle évite une image de fond, mais produirait un cadre beaucoup plus géométrique et moins détaillé que la référence.

## Portée et contraintes

- Rendre les deux écrans de démarrage à la résolution de la surface SDL/OpenGL, avec correspondance correcte des coordonnées de souris et des écrans à haute densité.
- Garder le menu interactif, le choix de langue existant et l’état désactivé de *Parallel Worlds*.
- Respecter `enable_audio`; ne pas ajouter de dépendance ni de musique.
- Ne pas modifier la résolution du jeu, ses filtres ou son rendu après le démarrage.

## Vérification prévue

Compiler avec le TCC Windows existant, capturer le tampon haute résolution pour inspection et confirmer le parcours jeu → langue ainsi que la navigation et les effets sonores.
