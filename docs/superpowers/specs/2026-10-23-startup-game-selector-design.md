# Choix du jeu et de la langue au démarrage

**État :** approuvée; sélecteur en cours d’implémentation

## Intention

Ajouter un écran de choix du jeu avant le sélecteur de langue actuel. L’écran montre les images des vrais boîtiers de *A Link to the Past* et *Parallel Worlds* fournies par le propriétaire. Le boîtier sélectionné est agrandi au centre; l’autre reste visible en retrait, avec une tranche, une ombre et un contour doré. Les jaquettes restent nettes, sans filtre ni titre artificiellement répété.

## Parcours et comportement

1. Au lancement, afficher les deux boîtiers et présélectionner le dernier jeu jouable choisi.
2. *A Link to the Past* est disponible. *Parallel Worlds* apparaît avec l’état « PORT EN COURS » et ne peut pas lancer de partie tant que son port natif n’est pas achevé.
3. Après confirmation du jeu disponible, afficher le sélecteur de langue existant. Conserver séparément le choix du jeu et celui de la langue.
4. Réutiliser la navigation clavier, manette et souris; fermer la fenêtre quitte le programme.

## Approches examinées

- **Sélecteur par étapes — retenu.** Ajouter l’écran maintenant et présenter *Parallel Worlds* comme en cours de port. Cela permet de construire le parcours demandé sans faire croire que le jeu est déjà jouable.
- **Attendre le port natif.** N’ajouter le sélecteur que lorsque les deux jeux sont disponibles. Ce choix évite une option temporairement inactive, mais reporte tout le parcours.

Le propriétaire du projet a choisi la première approche.

## Présentation des boîtiers

Ranger les images dans `assets/startup_covers/` et les charger au démarrage via l’imageur déjà intégré au projet. Le carrousel s’inspire des exemples de sélection Super Mario fournis : une boîte centrale dominante et une seconde boîte décalée. *Parallel Worlds* garde un bandeau `PORT EN COURS` et reste impossible à lancer.

## Portée et limites

La préférence `Game` sera enregistrée séparément de `Language` dans les réglages utilisateur. L’écran de sélection doit s’afficher avant le chargement des ressources propres au jeu. Aujourd’hui, `main()` appelle `LoadAssets()` avant l’initialisation de SDL et de la fenêtre; le démarrage devra donc être réordonné. La police bitmap de l’interface est statique dans `main.c`, ce qui permet de dessiner l’écran sans charger les ressources du jeu.

Ce changement ne porte pas *Parallel Worlds* : il ne modifie ni ses cartes, ni ses dialogues, ni ses routines ASM. Son choix deviendra actif lors d’un travail distinct qui ajoutera son chargement de ressources et ses comportements natifs.

## Conditions d’acceptation

- Le choix du jeu s’affiche avant le choix de langue et avant le chargement des ressources du jeu.
- Les deux jaquettes fournies sont visibles; le boîtier sélectionné est agrandi et facile à repérer; l’état « PORT EN COURS » reste évident pour *Parallel Worlds*.
- Confirmer *A Link to the Past* mène au sélecteur de langue actuel et conserve son comportement.
- Les préférences du jeu et de la langue sont sauvegardées indépendamment.
- La sélection fonctionne au clavier, à la manette et à la souris.
- La compilation Windows existante reste sans nouvelle dépendance.
