# État courant du projet

| Champ | Valeur |
|---|---|
| Date de vérification | 2026-08-06 |
| Méthode | Vérifications locales (git, build, ctest), validation visuelle sur poste, CI GitHub Actions |
| Version | 0.1.2 (tag `369277b`) — C++20, CMake ≥ 3.24, MSVC + Ninja, SDL3 + Dear ImGui (docking) |
| Sommet | `feature/imgui` @ `375e919` — arbre propre, **poussé (0/0)** le 06/08 à 15h15 |
| Chaîne de compilation | **CHANGÉE le 06/08** : Visual Studio désinstallé, remplacé par **Build Tools for Visual Studio 2026 18.8.2** (`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`). Toolset MSVC **14.51.36231**, `cl.exe` 19.51.36252 x64 — même famille qu'avant. Les scripts localisent la chaîne par `vswhere` via `build\vf-env.bat` : plus aucun chemin codé en dur |

À mettre à jour à chaque jalon. Discipline : distinguer
imaginé / validé / documenté / codé / compilé / testé / commité / poussé.

## Ce qui a changé les 05 et 06 août (31 commits, poussés)

**Performance de la préview exacte — le chantier central, terminé et mesuré.**
VF-0265 en treize lots a remplacé la copie du document et le remaillage complet
par un compositeur incrémental par chunks. Puis LOT 4c a ajouté un cache
d'overrides par chunk et supprimé le réenvoi GPU des chunks inchangés. Mesuré en
session réelle : `hl-plan` **13,8 → 0,2 ms**, `ho-exact` **10,1 → 0,0 ms**,
`highlights` **23,7 → 0,8 ms**. Budget tenu : p50 16,75 ms, **0,3 % de frames
hors budget** sur 13 000 frames.

**Instrumentation.** Gate 60 FPS avec percentiles réels (histogramme à pas fixe,
p50/p95/p99, sans allocation) ; quatre sous-sondes du handoff (`ho-configure`,
`ho-exact`, `ho-voxel`, `ho-transform`) ; sonde de latence du surlignage en
frames. **Correctif de métrique important** : le seuil strict à 16,67 ms comptait
la gigue du vsync comme un dépassement et annonçait 65 % de frames hors budget
sur une session saine — une tolérance de 1,5 ms ramène le chiffre à 0,3 %.

**ERGO-01, mission neuve.** Directive de Tony : tous les outils adoptent
l'ergonomie de MagicaVoxel. Cinq règles observées sur pièces (voir
`ERGONOMIE-BACKLOG.md`), plan en huit lots, trois lots faits :
lever le plafond de préview (garde sur le **delta**, plus sur le document),
corriger deux défauts de présentation, et donner la **préview exacte** à la
Ligne, à la Sphère et à la Boîte, qui n'affichaient que des contours filaires.

**Deux défauts visibles corrigés, que aucun test n'aurait trouvés.** Sur un 64³,
les voxels posés en maintenant le clic n'apparaissaient **qu'au relâchement** ;
et au-delà du plafond de delta, un trait long ne présentait **plus rien du tout**.
Les 149 tests vérifient l'état du document après commit, les 51 smokes vérifient
que l'éditeur ne casse pas — **aucun ne vérifie qu'on voit quelque chose pendant
le geste**. C'est la limite structurelle de la suite actuelle.

**Autres.** STAMP-25 (rotation à 45° par rééchantillonnage inverse, pas de
rotation faisant le tour complet), MODEL-01 (dimensions choisies à la création),
cache des index de palette du placement (67,4 → 0,066 ms à 1 M).

**Revue adversariale du 06/08** : 7 sous-systèmes, 14 agents, 8 bugs confirmés
sans faux positif. Aucun n'était connu. Voir `KNOWN-RISKS.md`.

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

**Fait depuis la version précédente de cette liste** : MODEL-01 (dimensions à la
création), le cache de palette du placement, et le compositeur incrémental de
préview exacte, qui a bien levé le plafond de 50 000 voxels — remplacé par une
garde sur le volume du delta.

Priorité immédiate, dans l'ordre :

1. **Deux bugs de gravité haute** de la revue du 06/08 : lecture hors-bornes
   dans Smart Fill (`SmartToolPlanner.cpp:779`) et écriture GPU hors-bornes au
   changement d'outil (`ViewportRenderer.cpp:1086`). Détail et scénarios dans
   `KNOWN-RISKS.md` ;
2. **Trois bugs de gravité moyenne à basse** : plafond `.vox` incohérent entre
   lecteur et écrivain (perte d'éditions), drapeau de ré-entrance bloqué sur
   exception, échec de sauvegarde signalé à tort ;
3. **ERGO-01 lots 4 à 7** — quadrillage de sélection, déplacement, rotation à
   angle libre, report numérique. **Bloqués par une observation manquante** : le
   panneau Edit de MagicaVoxel n'a pas pu être déplié, et le mode Face, Pattern,
   Voxel Shader et Region Select n'ont pas été testés. Les écrire sans ces
   observations serait deviner — deux prémisses sur trois se sont révélées
   fausses quand on l'a fait ;
4. **VF-0263** : redimensionnement de la boîte dans le viewport à la MagicaVoxel,
   plan validé, quatre lots, non commencé ;
5. **Étude d'architecture — rotation libre par instances de scène** (modèle
   VoxEdit) : un Stamp posé devient une instance avec sa matrice, jamais gravée
   dans la grille. Touche le format de sauvegarde, le rendu, l'undo/redo et
   l'export `.vox` → **document à valider avant toute ligne de code**. À noter :
   l'observation de MagicaVoxel a **validé** la conception de l'anneau à 45° — la
   référence fait de l'angle libre au degré avec accroche sur Shift, pas des
   crans imposés ;
6. Vérification de poste : bug grille §10.2 (candidat depth bias) ;
7. Phase D : Smart Tools — statut du gate SMART-02.5 à trancher (AR-0101 §15 le
   déclare implémenté, la roadmap le déclare requis) ;
8. **Piste neuve, non planifiée** : sur un 64³ plein, le poste dominant des
   frames lentes est désormais `vp-render` — le dessin réel de la scène — à 6 ms
   de médiane et jusqu'à 37 ms. Ce n'est plus la préview. À mesurer avant de
   conclure.

## Dette principale

`EditorWorkspace` n'est plus un God Object de logique mais un **harnais de
test** : 649 variables membres et 86 des 98 méthodes publiques sont des smokes.
`EditorWorkspaceSmoke.cpp` (7 346 l.) est désormais le plus gros fichier du
projet. Zéro TODO/FIXME dans tout `editor/src` et `engine`.
