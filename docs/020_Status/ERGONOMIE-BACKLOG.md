# Ergonomie des outils — carnet ouvert

Ouvert le 06/08/2026 à la demande de Tony. Ce n'est **pas** un plan de chantier :
c'est l'endroit où l'on note ce qui gêne à l'usage, au fur et à mesure, avant
d'en faire quoi que ce soit. Rien ici n'est engagé tant que Tony ne l'a pas
arbitré.

## Pourquoi un carnet séparé

La revue d'ergonomie est explicitement **hors périmètre** de la mission
PERF-FOUNDATION (§13 : pas de nouveaux outils d'UX, pas de refonte). C'était la
bonne séparation — mélanger perf et ergonomie aurait rendu les deux
inévaluables. Mais elle laissait les remarques d'usage sans endroit pour vivre.

## Déjà planifié ailleurs, ne pas dupliquer ici

- **VF-0263** — redimensionnement de la box dans le viewport, à la MagicaVoxel,
  avec le texte au-dessus. Plan validé, 4 lots, non commencé.
  Voir `docs/100_Architecture/Editor/VF-0263-Model-Box-Resize-Plan.md`.
- **Anneau de gizmo avec accroche à 45°** — différé.
- **Rotation libre par instances de scène** — étude d'architecture, différée.

## Remarques d'usage, à remplir

Format suggéré, un bloc par outil. Ce qui compte le plus est la troisième
ligne : ce qu'on ressent est plus utile qu'une solution proposée, parce que la
solution peut être meilleure que celle qu'on imagine.

```
### <outil>
Ce que je fais :
Ce qui se passe :
Ce que j'attendais :
Référence si applicable (MagicaVoxel, VoxEdit, autre) :
```

### Directive d'ensemble (Tony, 06/08/2026)

**Tous** les outils — création comme réutilisation — doivent adopter
l'ergonomie et l'utilisation de MagicaVoxel. Ce n'est pas une remarque sur un
outil isolé mais une ligne directrice : la référence est MagicaVoxel, et l'écart
à cette référence est le défaut.

Point de départ nommé explicitement : **la preview des voxels n'est pas bonne**.

Observation faite sur pièces le 06/08/2026 dans MagicaVoxel 0.99.7.2. Les règles
qui suivent sont **observées**, pas déduites. Ce qui n'a pas pu être observé est
listé à la fin, et doit rester non conclu.

### Erreur d'observation corrigée

Une première lecture rapide avait conclu que la preview de MagicaVoxel se
réduisait à « un carré rouge, une seule cellule visée, sans aperçu du résultat »,
et donc que notre preview exacte était **de trop**. C'était **faux**, et l'erreur
importe parce qu'elle aurait conduit à jeter deux jours de travail utile.

Le carré rouge n'est pas la couleur courante : avec la couleur courante réglée
sur bleu, le marqueur reste rouge. C'est une **ancre fixe d'un voxel, opaque, en
couleur codée en dur**, dessinée **par-dessus** la vraie preview. À taille de
brosse 1 elle la recouvre entièrement — d'où l'illusion.

Méthode qui a permis la correction : régler la couleur courante sur une teinte
contrastée avant d'observer. Une preview observée dans la couleur par défaut ne
permet pas de distinguer le contenu de l'interface.

### R1 — La preview EST le résultat

MagicaVoxel rend les voxels qui vont changer **déjà dans leur état final** :
position finale, couleurs réelles, ombrage normal des voxels, escalier exact.
Jamais translucide, jamais un contour seul, jamais une teinte de survol. Vérifié
sur l'outil Box : preview et résultat commité sont identiques.

**Conséquence pour nous.** Le concept de notre preview exacte est le bon — c'est
celui de la référence. Ce qui est mauvais est le **rendu** : nos `GhostVoxel`
translucides avec alpha. Il faut les rendre **opaques et identiques au résultat**,
pas les supprimer. PERF-02a, VF-0262, VF-0265 et LOT 4c ne sont pas du travail
perdu : ce sont eux qui rendent une preview exacte tenable à chaque frame.

### R2 — On ne dessine que le delta, et seulement ses faces visibles

La preview montre les **faces visibles** des voxels affectés. Une brosse
sphérique 3D sur un mur plat donne un **disque plat** : les voxels cachés
derrière, pourtant modifiés, ne sont pas montrés. Et si l'opération n'affecte
**aucun** voxel — hors bornes, rien à accrocher — la preview n'affiche **rien**.
Elle vaut donc aussi comme **retour de validité** : l'absence de preview dit que
le clic ne fera rien.

### R3 — L'ajout se prévisualise, le retrait non

Attach, Paint, Box, Line, Move et Rotate montrent leur résultat. La gomme et les
outils par région ne montrent **que l'ancre rouge** : rien n'indique quels voxels
vont disparaître. Lecture cohérente : le moteur ne sait dessiner que de la
géométrie positive.

À arbitrer par Tony : c'est une limite de MagicaVoxel autant qu'un choix. Copier
ce comportement est défendable au nom de la cohérence, mais prévisualiser une
suppression est un vrai progrès d'ergonomie. Ne pas trancher par mimétisme.

### R4 — Deux surcouches d'interface, couleurs fixes, toujours au-dessus

1. **Ancre rouge d'un voxel** : le voxel sous le curseur, ou le coin mobile d'un
   glissement. Non centrée pour les tailles de brosse paires.
2. **Pour les sélections** : quadrillage blanc voxel par voxel sur les faces
   sélectionnées, plus le contour de la boîte. Couleur paramétrable (« Selection »
   dans Display).

Ce sont des couleurs d'**interface**, indépendantes de la palette. Corollaire :
à taille 1 l'ancre masque la preview, et c'est voulu — ne pas « corriger ».

### R5 — Les opérations continues sont live, snappées, et chiffrées

Box, ligne, déplacement et rotation se mettent à jour à chaque mouvement, se
calent sur la grille, et reportent leur état **numériquement** : coordonnées
voxel, offset `x/y/z`, `angle:`. Un offset ramené à zéro restaure exactement
l'état d'origine.

### Déplacement d'une sélection — le comportement de référence

C'est la « réutilisation » au sens de Tony. La matière apparaît **à sa nouvelle
place, entièrement opaque, dans ses vraies couleurs**, avec le quadrillage de
sélection par-dessus. À l'ancienne place il y a un **vrai trou**, cerné d'un
contour blanc, à travers lequel on voit le fond. **Aucun fantôme, aucune
translucidité, aucune copie « avant ».** Déplacement snappé au voxel, verrouillé
sur un axe (deux avec Ctrl+Shift), offset affiché, entièrement réversible en
direct.

### Rotation — le comportement de référence

Gizmo de type trackball centré sur la sélection, plus grand que la sélection
elle-même : un cercle extérieur aligné écran, deux cercles d'axes colorés, et une
lentille fine qui passe au rouge quand l'axe est actif. **Angle libre au degré**,
accroche sur **Shift**, valeur affichée en bas à gauche. Le résultat tourné est
**revoxelisé en direct et dessiné opaque**.

Cela **valide la conception de l'anneau à 45°** qu'on avait différée : la
référence fait de l'angle libre avec accroche optionnelle, pas des crans
imposés.

### Ce qui n'a PAS été observé — ne pas conclure dessus

- Sections dépliables du panneau Edit (Rotate 90°, Flip, Loop, Scale, Shear,
  Repeat, Mirror, Diagonal, Shape, Modify) : impossible de les déplier à la
  résolution disponible. On ignore donc lesquelles offrent un aperçu. Constat
  partiel : les boutons simples s'appliquent **instantanément sans aperçu ni
  confirmation** — `Clear` a vidé le modèle en un clic.
- Mode Face (extrusion par glissement), Pattern, Voxel Shader.
- Region Select (`N`) : visuel non testé, seul le marquee `M` l'a été.
- Previews de Scale et de Wrap ; le gizmo « Show Brush Gizmo ».
- Si `Esc` annule une rotation **pendant** le glissement.
- Si la gomme respecte la taille de brosse : le modèle uniformément blanc rendait
  le résultat invisible.

## Comment alimenter ce carnet efficacement

Tony a proposé deux moyens qui valent mieux qu'une description écrite, et qui
sont retenus :

1. **De courtes vidéos** du geste qui pose problème. Voir la main faire est plus
   informatif que lire ce qu'elle voulait faire.
2. **Les logiciels de référence installés** — MagicaVoxel et VoxEdit — pour
   montrer directement le comportement attendu au lieu de le décrire.

Dans les deux cas, la question à se poser est : *quel geste* est pénible, et
*combien de fois par heure* on le fait. Un défaut d'ergonomie coûte le produit
de ces deux termes, et c'est ce produit qui doit décider de l'ordre des
chantiers — pas l'agacement du moment.
