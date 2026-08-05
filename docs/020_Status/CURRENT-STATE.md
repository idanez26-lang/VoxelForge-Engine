# État courant du projet

| Champ | Valeur |
|---|---|
| Date de vérification | 2026-08-05 |
| Méthode | Vérifications locales (git, build, ctest), validation visuelle sur poste, CI GitHub Actions |
| Version | 0.1.2 (tag `369277b`) — C++20, CMake ≥ 3.24, MSVC + Ninja, SDL3 + Dear ImGui (docking) |
| Sommet | `feature/imgui` @ `3a59e9e` — arbre propre, poussé (0/0) |

À mettre à jour à chaque jalon. Discipline : distinguer
imaginé / validé / documenté / codé / compilé / testé / commité / poussé.

## Branches

| Branche | État |
|---|---|
| `main` | Starter kit v0.0.1, aucun code moteur |
| `feature/imgui` | **Branche de travail unique** |
| `experiment/viewport-interaction-v2` | Fusionnée en avance rapide, supprimée |
| `feature/common-foundation` | Supprimée (ancêtre strict confirmé) |

## Chantiers clos

**VF-0260 — dégraissage `EditorWorkspace.cpp`** : 17 398 → **7 199 lignes (−58,6 %)**.
Lots 0 à 7d commités ; nettoyage final (assistants ImGui mutualisés dans
`EditorWorkspaceUiHelpers.h`, noms de panneaux sourcés depuis
`Layout/EditorPanelNames.h`) fait le 03/08.
Reste optionnel : sortir l'état des smokes de la classe (cf. KNOWN-RISKS).

**VF-0261 / PERF-02 — performance de la préview outils** : frames de dessin
258 → ~20-35 ms. Préview agrégée au-delà de 256 cellules, rebuild différé
pendant le trait, préview exacte plafonnée à 50 000 voxels (option A),
coalescence des highlights (1 résolution/frame), sondes `EditorFrameProbe`
(17 postes) + journal `voxelforge-perf.log`.

**VF-0262 — rebuild incrémental du mesh par chunks 32³** : clos le 03/08.
Journal d'invalidation `ChangesSince` dans `VoxelDocument`, cache par chunks,
itération régionale du builder, upload GPU partiel, assemblage paresseux.
Résultat : **édit ~4-22 ms de 15k à 1M voxels** (vs 836 ms de rebuild complet
à 1M, ×38). Session sonde réelle : 2 frames lentes sur toute une session
d'édition, mesh-sync 1,5-5,3 ms ×1, gpu-upload 5-9,5 ms ×1.

**Voxel Stamps V1 (STAMP-01 → STAMP-23)** : workflow complet — capture,
bibliothèque projet et My Library, catalogue, cache borné, planificateur de
placement, preview exacte, placement atomique, session continue,
transformations rapides, Forge Library, Smart Variants, Smart Placement,
durcissement perf (VF-0252), portes de sortie et guide utilisateur (VF-0253).
Deux bugs de terrain corrigés le 04/08 avant validation :
génération de document unifiée (le placement était refusé in extremis) et
arrondi demi-voxel des rotations (tout Stamp de dimension impaire était
irrotable).

**STAMP-24 — rotation des Stamps sur les trois axes** : clos le 05/08.
24-1 moteur (`StampPlacementRotationAxis` : `VerticalY`, `LateralX`, `DepthZ` ;
permutations exactes ; arrondi demi-voxel généralisé), 24-2 session + UI
(sélecteur d'axe, trois anneaux du gizmo déverrouillés, l'anneau saisi décide
de l'axe), 24-3 Smart Placement aligné sur l'axe choisi.

**STAMP-25 — pas de rotation 90° / 45°** : clos le 05/08. 25-1 moteur
(rééchantillonnage par mapping inverse destination → source, donc sans trou ;
`HalfQuarterStep` dans le transform, `Statistics.ApproximateRotation` et
diagnostic `ApproximateRotation` non bloquant ; garde-fou à 8 M cellules).
25-2 interface : `Q` / `Shift+Q` avancent d'un cran et font le tour complet —
quatre positions au pas de 90°, huit au pas de 45° ; le pas se choisit dans Tool
Options ou par `Shift+E` et ne modifie jamais l'orientation courante. L'écart
`N source voxels -> M cells` est affiché en permanence, sans boîte de
confirmation. L'anneau du gizmo reste aimanté aux quarts de tour.

## Tests et build

- Suite bloquante locale : **144/144** ; smokes GUI `EditorApp` : **51/51**,
  **bloquants en CI** depuis `5650127` ;
- CI verte ; les smokes GUI tournent réellement sur le runner (D3D12 WARP)
  depuis la correction des noms courts 8.3 du `%TEMP%` (`379cb5a`) ;
- `Application` retourne **77** si l'init vidéo/GPU échoue → `SKIP_RETURN_CODE`
  sur la suite EditorApp (postes sans GPU) ;
- ⚠️ Suites locales : `TMP`/`TEMP` redirigés vers `E:\VoxelForge-Engine\build\tmp` ;
- ⚠️ **Visual Studio fermé** pendant tout configure/build en ligne de commande ;
- `tests/CMakeLists.proposed.txt` : obsolète, à régénérer avant adoption.

## Validations produit

| Gate | État |
|---|---|
| Voxel Stamps V1 — technique (VF-0253, G0-G4, G6-G8) | ✅ Pass |
| Voxel Stamps V1 — produit (G5) | ✅ **Validé par Tony le 04-05/08** : placement complet en un clic (4 346 cellules, Undo/Redo atomiques), aperçu sans côtés manquants, rotation sur les trois axes |

## À faire

1. **Dimensions de la box à la création** (en cours) : le dialogue nom +
   dimensions existe déjà et valide 1..256, mais `RequestNewVoxelModelDialog()`
   est du code mort — *New Model* et `Ctrl+Shift+N` passent par la création
   instantanée figée à 64³. Décision Tony du 05/08 : le dialogue s'ouvre à
   chaque création. Le redimensionnement d'un modèle existant est un lot séparé ;
2. **Étude d'architecture — rotation libre par instances de scène** (modèle
   VoxEdit) : un Stamp posé devient une instance avec sa matrice, jamais
   gravée dans la grille. Touche le format de sauvegarde, le rendu, l'undo/redo
   et l'export `.vox` → **document à valider avant toute ligne de code** ;
3. Mini-lot perf : cache des index de palette occupés dans
   `StampPlacementPlanner` (supprime un parcours O(document) par frame de
   placement) ;
4. Compositeur incrémental de préview exacte (« option B » de VF-0261) — lève
   le plafond de 50 000 voxels ;
5. Gizmo aimanté aux huitièmes de tour (l'anneau ne connaît que les quarts,
   alors que le clavier fait des crans de 45°) — le gizmo est partagé avec les
   outils de sélection, d'où un lot dédié ;
6. Vérification de poste : bug grille §10.2 (candidat depth bias) ;
7. Phase D : Smart Tools — statut du gate SMART-02.5 à trancher (AR-0101 §15 le
   déclare implémenté, la roadmap le déclare requis).

## Dette principale

`EditorWorkspace` n'est plus un God Object de logique mais un **harnais de
test** : 649 variables membres et 86 des 98 méthodes publiques sont des smokes.
`EditorWorkspaceSmoke.cpp` (7 346 l.) est désormais le plus gros fichier du
projet. Zéro TODO/FIXME dans tout `editor/src` et `engine`.
