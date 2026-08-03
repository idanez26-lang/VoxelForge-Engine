# État courant du projet

| Champ | Valeur |
|---|---|
| Date de vérification | 2026-08-01 |
| Méthode | Vérifications locales (git, build, ctest) + CI GitHub Actions |
| Version | 0.1.2 (tag `369277b`) — C++20, CMake ≥ 3.24, MSVC + Ninja, SDL3 + Dear ImGui (docking) |

À mettre à jour à chaque jalon. Discipline : distinguer
imaginé / validé / documenté / codé / compilé / testé / commité / poussé.

## Branches

| Branche | État |
|---|---|
| `main` | Starter kit v0.0.1, aucun code moteur |
| `feature/imgui` | **Branche de travail unique** — chantiers PERF-02 (VF-0261) livrés : préview agrégée, rebuild différé pendant le trait, préview exacte plafonnée à 50k voxels (option A) ; VF-0262 lancé (262-1 : builder par région commité) ; `feature/common-foundation` supprimée |
| `experiment/viewport-interaction-v2` | Fusionnée en avance rapide dans `feature/imgui`, supprimée (locale + distante) |
| `feature/common-foundation` | Ancêtre strict de `feature/imgui` — archivage/suppression : décision Tony en attente |

## Chantier VF-0260 — dégraissage EditorWorkspace.cpp

`EditorWorkspace.cpp` : 17 398 lignes (31/07) → **7 086 lignes (02/08, −59,3 %)**.

| Lot | Contenu | État |
|---|---|---|
| 0 | Harnais smoke → `EditorWorkspaceSmoke.cpp` | ✅ Commité, CI verte |
| 1 | Console → `EditorConsoleService` (+ test) | ✅ Commité, CI verte |
| 2 | Layout dock → `EditorDockLayout` + `EditorPanelNames.h` | ✅ Commité, CI verte |
| 3 | Stamps → `StampPreviewController` (drapeau `voxelEditInProgress_` partagé par référence) | ✅ Commité, CI verte |
| 4a | Mapping pur session↔outils → `ProjectSession/ProjectSessionMapping.{h,cpp}` + test riche ; règle legacy Cube/Sphère→Pencil préservée | ✅ Commité `d383023`, CI #11 verte |
| 4b | `ProtectedProjectDeletionRoots` → `ProjectDeletionService::DefaultProtectedRoots()` (statique) + test de garde | ✅ Commité `d383023`, CI #11 verte |
| 4c | `SynchronizeProjectAssets` (~80 l., orchestration de ~15 services) | ✅ Clos par décision : **statu quo assumé** (Tony, 01/08) — orchestration légitime, un contrôleur à 15 références serait pire |
| 5 | Import : machine à états `ModelImport/ModelImportBatch` (file, compteurs, collisions, décisions de fin) + test dédié ; fermeture examinée → déjà factorisée (`dirtyActionConfirmation_`/`closeRequest_`), rien d'extractible | ✅ Commité `0afa2cf`, CI verte (8/8 ctest ciblés en local) |
| 6 | Adaptateurs transforms (23 méthodes : appliers panneau, ponts contraintes, Begin/Apply/Cancel Move/Duplicate/Rotate/Mirror/Scale/Align, annulation gizmo) déplacés en TU dédiée `EditorWorkspaceTransforms.cpp` — pur déplacement, comportement inchangé | ✅ Commité `43f99a1` (128/128 ctest bloquants en local) |
| 7 | 7a sondes `sp-*` ✅ ; 7b coalescence highlights (1 résolution/frame) ✅ ; 7c `UpdateVoxelHighlights`+`ForceVoxelHighlightsResolve` → `EditorWorkspaceHighlights.cpp` ✅ ; 7d `DrawScenePanel` (1 576 l.) → `EditorWorkspaceViewportPanel.cpp` ✅ (`71cfabc`). Reste optionnel : stroke Smart Tool en TU, extractions de services | 🟢 Quasi clos |

## Tests et build

- Build Debug complet vert (01/08, poste local, x64) ; suite bloquante 128/128 (lot 6) ;
- Nouveaux tests : `VoxelForge.Editor.ProjectSessionMapping` (lot 4),
  `VoxelForge.Editor.ModelImportBatch` (lot 5) ;
- CI : verte jusqu'à `0afa2cf` inclus (lot 6 `43f99a1` : run en cours au moment de cette note) ;
  l'étape `EditorApp` (GPU requis) échoue sur runner comme attendu (non bloquante) ;
- ⚠️ Suites locales : lancer ctest avec `TMP`/`TEMP` redirigés vers
  `E:\VoxelForge-Engine\build\tmp` — le `%TEMP%` de C: provoque des `Access is denied`
  (ACL/Defender), vu sur VoxelForge.Editor.StampCatalog ;
- `tests/CMakeLists.proposed.txt` : obsolète depuis les lots (à régénérer avant adoption).

## Environnement de build local (leçons du 01/08)

- **Visual Studio fermé pendant tout configure/build en ligne de commande** : `devenv` en
  arrière-plan entre en concurrence sur le `binaryDir` (index `.vs`) et provoque un
  `rules.ninja` jamais écrit, sans aucune erreur CMake. Un seul propriétaire du répertoire
  de build à la fois.
- CMake **4.4.1 standalone** (`C:\Program Files\CMake\bin\`) en service ; environnement x64
  via `"E:\visual studio\Common7\Tools\VsDevCmd.bat" -arch=x64` (la Developer PowerShell
  par défaut initialise x86).
- Workflow de collaboration : Claude (Cowork) = code et fichiers ; Codex = exécution
  console (builds, git) sur prompts validés par Tony. Aucun commit/push sans validation.

## À faire (hors lots)

1. **PERF-02 livré (VF-0261)** : frames de dessin 258 → ~35 ms mesurés. Restes
   documentés : sections `sp-*` de DrawScenePanel (~17-29 ms fantômes), matérialisation
   paresseuse du planner, coalescence (secondaire). **VF-0262 en cours** : 262-0 ✅
   (benchmark, baseline `688cea6`), 262-1 ✅ (builder par région + équivalence),
   262-2 ✅ (`6956f75`, journal d'invalidation `ChangesSince` dans VoxelDocument),
   262-3 ✅ (`b387230`, cache incrémental chunks 32³ + itération régionale :
   région 32³ plate à ~8,8 ms, édit 823→21 ms @1M, ctest 129/129),
   262-3bis ✅ codé le 03/08 (drapeau d'occupation : recoloration sans
   invalidation des voisins — en attente build/commit) ; reste 262-4
   (upload partiel renderer) ;
2. Vérifications de poste : smoke GUI, bug grille §10.2 (candidat : depth bias) ;
3. Mini-lot différé : skip propre des tests `EditorApp` en CI (`SKIP_RETURN_CODE`) pour
   supprimer l'annotation d'erreur cosmétique ;
4. Nettoyage différé : copies de noms de panneaux **+ 5 symboles dupliqués au lot 7d
   (`ViewportPanelWindowName`, `DrawErrorMessage`, `DrawTooltip`,
   `DrawSelectionHandles`, `SetSelectionHandleCursor`) — à mutualiser dans un
   en-tête**, régénérer `tests/CMakeLists.proposed.txt`,
   lot 3-bis (`BeginSaveSelectionAsStamp` + dialogue stamps), presenter des dialogues projet
   (facultatif, post-4a/4b) ;
5. Phase D ensuite : Smart Tools — gate SMART-02.5 avant SMART-03 (AR-0104).

## Dette principale

`EditorWorkspace.cpp` ≈ 9 180 lignes (God Object en résorption, −47,2 % depuis le 31/07) —
reste le lot 7 de VF-0260 (DrawScenePanel, UpdateVoxelHighlights, stroke Smart Tool),
à coupler avec le verdict PERF-01.
