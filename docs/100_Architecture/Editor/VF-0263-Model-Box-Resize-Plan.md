# VF-0263 — Redimensionner la boîte du modèle depuis le viewport

**État : plan validé par Tony le 05/08. Chantier ajourné au profit de la
fondation performance (mission du 05/08). Aucune ligne de code écrite.**
Branche de travail : `feature/imgui`.

## 1. Ce que Tony a demandé

> « le changement de box doit se faire comme MagicaVoxel, directement changer sur
> le viewport »
> « je ne veux pas de redimensionnement du model, juste la box qui change de
> taille »
> « comme Magica, à texte au-dessus dans le viewport »

Arbitrages du 05/08 :

1. **Le contenu ne bouge pas.** On change le volume éditable, pas le modèle. Un
   voxel en `(4, 2, 6)` reste en `(4, 2, 6)`. Agrandir ajoute du vide,
   rétrécir coupe ce qui dépasse. **Aucun rééchantillonnage**, aucune marche
   d'escalier, aucune perte de fidélité sur ce qui reste.
2. **Rognage annulable.** Ce qui sort du cadre disparaît sans boîte de
   dialogue ; `Ctrl+Z` restaure la boîte **et** les voxels coupés en une seule
   fois.

Non concerné par ce chantier : mettre le modèle à l'échelle (×2, ÷2, facteur
libre). C'est un autre outil, à traiter séparément si le besoin vient.

## 2. État des lieux (ce qui existe, ce qui manque)

| Brique | État |
|---|---|
| Dimensions du sous-modèle | `VoxelSubModel::dimensions_` **privé**, aucun mutateur ; seuls `VoxelDocument` et `VoxDocumentLoader` sont amis |
| Stockage des voxels | `unordered_map<VoxelPosition, Voxel>` — les positions ne changent pas, seules celles hors du nouveau cadre sont retirées |
| Journal d'invalidation (VF-0262) | par position ; un changement de dimensions le rend **inexploitable** → il faudra forcer le repli « rebuild complet » |
| Historique | `VoxelEditOperation` = étiquette + liste de `VoxelChange` (+ sélection, + palette). **Rien pour un changement structurel** |
| Poignées dans le viewport | `SelectionHandleModel` + `SelectionInteraction` pour la **sélection** uniquement ; la boîte du modèle n'est qu'un décor |
| Création | dialogue nom + dimensions (1..256) — câblé par MODEL-01 |

Bonne nouvelle du choix « le contenu ne bouge pas » : le lot moteur devient
simple. Agrandir ne touche à **aucun** voxel ; rétrécir n'est qu'une suppression
des positions hors cadre, c'est-à-dire exactement ce que l'historique sait déjà
enregistrer.

## 3. Découpage proposé

### 263-1 — Redimensionnement dans le document (moteur, sans UI)

- `VoxelDocument::ResizeModel(modelIndex, VoxelDimensions)` : change les
  dimensions et retire les voxels hors du nouveau cadre. Rien d'autre.
- Bornes 1..256 par axe, comme à la création. Mêmes dimensions = aucun
  changement, aucune révision consommée.
- L'origine reste fixe : la boîte grandit ou rétrécit **par ses faces max**
  quand on passe par un champ numérique ; dans le viewport, c'est la face tirée
  qui bouge (lot 263-3).
- Journal : la mutation enregistre une **rupture** (le journal répond `nullopt`)
  pour que `VoxelDocumentMeshCache` reparte sur un rebuild complet.
- Tests : agrandissement (aucun voxel touché, bornes recalculées),
  rétrécissement (seuls les voxels hors cadre partent), rétrécissement sans
  perte, axes indépendants (32×32×32 → 64×32×16), volume vide, bornes 1 et 256,
  idempotence, dimensions écrites correctement à l'export `.vox`.

### 263-2 — Opération annulable

- Nouveau bloc optionnel dans `VoxelEditOperation` :
  `DimensionChange { Before, After }` (`shared_ptr`, comme la palette).
- Les voxels coupés voyagent dans les `Changes` habituels : l'Undo les remet en
  place après avoir restauré les dimensions. Ordre imposé : dimensions d'abord,
  voxels ensuite ; inverse à l'Undo.
- Un agrandissement ne coûte donc **rien** en mémoire d'historique ; seul le
  rétrécissement destructeur pèse, à hauteur de ce qu'il détruit.
- Tests : Undo/Redo d'un agrandissement, d'un rétrécissement destructeur, et
  d'une suite redimensionnement + dessin + Undo.

### 263-3 — Poignées de boîte dans le viewport

- Cadre du modèle rendu comme volume manipulable : six poignées de face, à la
  MagicaVoxel.
- Réutilise le schéma de `SelectionInteraction` : capture, glissement contraint
  à l'axe, aperçu, validation au relâchement.
- Pendant le glissement : **aucune mutation**. On n'affiche que le cadre cible
  et les dimensions résultantes ; l'opération est construite au relâchement,
  sinon on reconstruit tout le mesh à chaque frame.
- Retour visuel quand le cadre passe sous de la matière : la portion qui sera
  coupée s'affiche en rouge tant que le bouton est enfoncé. L'artiste voit ce
  qu'il perd avant de lâcher, sans qu'on lui impose une confirmation.
- **Étiquette de dimensions dans le viewport**, à la MagicaVoxel : le volume
  courant s'affiche en texte au-dessus de la boîte (`64 x 32 x 64`), ancré sur
  le sommet du cadre et suivant la caméra. Pendant un glissement il affiche la
  valeur cible et l'écart (`64 x 32 x 64  ->  96 x 32 x 64`), en rouge si le
  mouvement coupe de la matière.
- L'étiquette est **cliquable** : un clic ouvre une saisie directe des trois
  valeurs dans le viewport, validée par `Enter`, annulée par `Esc`. Même
  opération que les poignées, donc même Undo.
- Contraintes : 1..256 par axe, snap au voxel.
- Le champ numérique du panneau reste disponible et emprunte la même opération.

### 263-4 — Finitions

- Message console (« Model box resized: 32×32×32 → 64×32×64, 128 cells
  removed »), guide utilisateur, journal des décisions.

## 4. Risques identifiés

1. **Coût du rebuild** : un redimensionnement invalide tout le mesh. Sur 256³ le
   rebuild complet se compte en centaines de millisecondes ; acceptable pour une
   action ponctuelle, à condition de ne pas le déclencher pendant le glissement
   — d'où l'aperçu en fil de fer du lot 263-3.
2. **Mémoire de l'historique** : un rétrécissement massif sur un modèle dense
   enregistre tous les voxels détruits. Le plafond de 256 Mo de l'historique est
   à vérifier avec des chiffres réels au lot 263-2 (refus propre plutôt que
   dépassement silencieux).
3. **Repères de la scène** : plan de travail, grille et caméra dépendent des
   dimensions. Ils doivent suivre le nouveau cadre au même instant, sinon la
   grille flotte hors du modèle.
4. **Export `.vox`** : les dimensions changent dans le fichier ; l'écrivain et
   les vignettes doivent suivre (test au lot 263-1).

## 5. Ordre et coût

263-1 (moteur, court maintenant que le contenu ne bouge plus) → 263-2
(historique) → 263-3 (viewport, le gros du travail) → 263-4. Chaque lot est vert
sur la suite bloquante avant le suivant, et rien n'est commité avant validation
visuelle de Tony.
