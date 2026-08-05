# Voxel Stamps V1 — Guide utilisateur et diagnostic

## À quoi servent les Voxel Stamps ?

Un Voxel Stamp transforme une sélection de voxels en création réutilisable.
La géométrie, les couleurs, le pivot et l’identité de la création sont conservés
dans un fichier source `.vfstamp`. La Forge Library sert à retrouver ces
créations et à lancer leur placement.

Deux espaces sont séparés :

- **Project Library** : créations portables avec le projet, sous
  `Assets/ForgeLibrary/Creations` ;
- **My Library** : créations personnelles disponibles pour le profil local,
  sans chemin absolu enregistré dans le projet.

Les fichiers `.vfstamp` restent la source de vérité. Le catalogue JSON n’est
qu’un index dérivé : il peut être reconstruit avec **Refresh**.

## Créer et utiliser un Stamp

1. Ouvrir un modèle voxel et sélectionner les voxels à réutiliser.
2. Choisir **Save Selection As Stamp**.
3. Donner un nom à la création et confirmer le pivot proposé.
4. Ouvrir **Forge Library**, choisir **Project** ou **My**, puis rechercher la
   création.
5. Sélectionner la carte et cliquer **USE STAMP**, ou double-cliquer la carte.
6. Le panneau **Tool Options** devient **Stamp Placement**. Utiliser le gizmo
   **Move** pour déplacer précisément la preview sur les axes X, Y et Z.
   Choisir **Rotate Y** pour la tourner avec l’anneau.
7. Vérifier la preview exacte puis appuyer sur **Enter** ou choisir
   **PLACE FULL STAMP** dans Tool Options. Le Stamp complet est confirmé comme
   une seule opération Undo/Redo.
8. Appuyer sur **Esc** pour terminer. Chaque placement reste une opération
   Undo/Redo indépendante et atomique, couleurs comprises.

### Contrôles pendant la preview

| Action | Contrôle |
|---|---|
| Déplacer la preview | glisser les axes X, Y ou Z du gizmo **Move** |
| Tourner avec le gizmo | choisir **Rotation gizmo**, puis glisser l’anneau X, Y ou Z |
| Choisir l’axe de rotation | radios **X / Y / Z** dans Tool Options (un seul axe actif) |
| Placer une copie | `Enter` ou bouton **PLACE FULL STAMP** dans Tool Options |
| Tourner d'un cran | `Q` / `Shift+Q` |
| Choisir le pas (90° ou 45°) | radios **Rotation step** dans Tool Options, ou `Shift+E` |
| Miroir X ou Z | `X` / `Z` |
| Parcourir les miroirs | `M` |
| Réinitialiser rotation et miroir | `Shift+M` |
| Terminer le placement | `Esc` |

La preview verte est valide. L’orange signale un chevauchement autorisé : seules
les cellules réellement superposées seront remplacées avec la politique par
défaut. Le rouge signale un état bloquant ; aucun voxel n’est alors modifié.

**Rotation d’un Stamp de dimension impaire.** Le pivot centré d’un Stamp dont la
largeur ou la profondeur est impaire tombe sur un demi-voxel (par exemple un mur
`18 × 24 × 49` a son pivot en Z à 24,5). Une rotation de 90° ou 270° échange X et
Z : ce demi-voxel change d’axe et la position n’est plus exactement sur la
grille. Le placement l’arrondit alors au voxel le plus proche. Le décalage étant
identique pour toutes les cellules du Stamp, la forme n’est jamais déformée — le
Stamp atterrit simplement un demi-voxel plus loin sur l’axe concerné. Les
rotations de 0° et 180° restent exactes, et une cible sous-voxel demeure une
erreur explicite.

**Pas de rotation : 90° ou 45°.** Le pas n’est qu’un incrément — dans les deux
cas `Q` et `Shift+Q` font le tour complet, en quatre crans (0, 90, 180, 270) ou
en huit (0, 45, 90 … 315). Le pas se change à tout moment sans perdre
l’orientation courante.

Les positions multiples de 90° sont des permutations exactes de la grille :
aucune matière n’est perdue. Les positions impaires (45, 135, 225, 315) ne sont
pas des symétries du réseau cubique — la diagonale mesure √2 fois le côté. Le
placement rééchantillonne alors le Stamp : il parcourt les cellules d’arrivée et
va chercher, pour chacune, le voxel source dont elle provient. Ce sens de
parcours garantit l’absence de trous, mais le nombre de cellules obtenues
diffère du nombre de voxels source et les arêtes sont redessinées en escalier.

L’écart est annoncé en permanence plutôt que confirmé par une boîte de dialogue :
Tool Options affiche `N source voxels -> M cells` en orange dès que la rotation
est approximative, et la console rappelle l’écart à chaque cran. Le placement
reste autorisé — VoxelForge informe, il n’interdit pas. Un cran de plus retombe
sur une position exacte, et `Shift+M` (reset transform) revient à 0°.

L’anneau du gizmo de rotation reste aimanté aux quarts de tour, quel que soit le
pas choisi : les crans de 45° passent par `Q` / `Shift+Q`.

## Smart Placement et Smart Variants

Smart Placement est actif par défaut en mode d’assistance de preview. Il peut
proposer un alignement de surface, un pivot ou une orientation, mais reste
consultatif : une rotation explicite, la désactivation ou le contournement
temporaire de l’utilisateur gagne toujours. Une suggestion ne peut pas rendre
obligatoire un placement qui serait refusé.

Les groupes Smart Variants choisissent une identité exacte et déterministe. Le
choix effectué est enregistré dans l’opération : Undo/Redo ne relance jamais le
tirage, même si le fichier source choisi disparaît après le placement.

## Comprendre et réparer les échecs

| Symptôme | Signification | Action sûre |
|---|---|---|
| Catalogue indisponible ou JSON invalide | l’index dérivé est absent/corrompu | utiliser **Refresh** pour reconstruire depuis les `.vfstamp` |
| Source manquante | la carte référence un fichier supprimé/déplacé | restaurer le `.vfstamp` ou reconstruire le catalogue |
| Source invalide/corrompue | le conteneur, le checksum ou le contenu sémantique est refusé | conserver le diagnostic, remplacer la source ; le document n’est pas modifié |
| Preview rouge | limites, hors-bounds, palette ou politique de collision bloquante | déplacer la preview ou corriger le contexte ; aucun commit n’est créé |
| My Library indisponible | le répertoire local du profil n’est pas accessible | vérifier le profil local ; le projet reste indépendant |
| Limite de ressources | le Stamp dépasse un budget dur V1 | réduire la création ; le rejet intervient avant l’allocation proportionnelle |

La reconstruction d’un catalogue ne supprime jamais les sources invalides :
elles sont exclues de l’index avec un diagnostic afin de permettre leur
récupération.

## Vérification manuelle V1

Avant de déclarer le parcours produit validé :

1. créer une sélection multicolore reconnaissable avec des trous ;
2. l’enregistrer et la retrouver par recherche dans Forge Library ;
3. vérifier le pivot et la preview exacte ;
4. placer une copie dans le vide puis une copie en chevauchement orange ;
5. confirmer que seules les cellules superposées sont remplacées ;
6. placer plusieurs copies, terminer avec Esc, puis Undo/Redo chaque copie ;
7. enregistrer, fermer et rouvrir le modèle ;
8. copier le projet à un autre emplacement et vérifier la Project Library.

## Diagnostics automatisés de release

Les modes suivants sont headless : ils démarrent avant le GPU et utilisent
uniquement des racines projet/profil temporaires injectées, supprimées avant la
fin du processus.

```powershell
VoxelForgeEditor.exe --voxel-stamp-smoke-test
VoxelForgeEditor.exe --voxel-stamp-corruption-smoke-test
VoxelForgeEditor.exe --voxel-stamp-library-smoke-test
VoxelForgeEditor.exe --voxel-stamp-variant-smoke-test
VoxelForgeEditor.exe --voxel-stamp-smart-placement-smoke-test
```

Le premier mode traverse le parcours complet : capture, installation,
recherche, preview, deux placements, Esc, deux Undo/Redo, sauvegarde,
réouverture, nouvelle session et déplacement du projet. Les autres isolent les
échecs afin qu’un problème de corruption, de portée, de variante ou de Smart
Placement soit immédiatement identifiable.

## Limites V1

- échelle de placement strictement `1:1` ;
- pas de marketplace, cloud, partage ou Asset Packs ;
- pas de déformation de l’environnement ni de génération IA ;
- le catalogue et les miniatures sont dérivés, jamais la source de vérité ;
- le placement garde la fidélité exacte plutôt que de simplifier silencieusement
  un Stamp pour respecter un budget ;
- **un seul axe de rotation actif à la fois** : X, Y et Z sont disponibles
  (STAMP-24) mais ne se composent pas. Changer d’axe remet l’angle à zéro plutôt
  que de réinterpréter l’angle courant sur le nouvel axe ;
- **rotation libre non disponible** : seuls les pas de 90° et de 45° (STAMP-25)
  existent. Une rotation d’angle quelconque suppose des instances de scène à la
  VoxEdit, chantier ultérieur.
