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
| `feature/imgui` | **Branche de travail unique** — sommet `43f99a1` (lot 6) |
| `experiment/viewport-interaction-v2` | Fusionnée en avance rapide dans `feature/imgui`, supprimée (locale + distante) |
| `feature/common-foundation` | Ancêtre strict de `feature/imgui` — archivage/suppression : décision Tony en attente |

## Chantier VF-0260 — dégraissage EditorWorkspace.cpp

`EditorWorkspace.cpp` : 17 398 lignes (31/07) → **9 180 lignes (01/08, −47,2 %)**.

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
| 7 | `DrawScenePanel` (1 536 l.) + `UpdateVoxelHighlights` (628 l.) + stroke Smart Tool — invariants sensibles, gardé pour la fin | ⚪ À venir |

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

1. **PERF-01 clos → PERF-02 planifié (VF-0261)** : cause du lag outils identifiée par
   sonde in-app — `UpdateVoxelHighlights` croît avec le volume de préview (255 ms/appel
   à pinceau 64, dont 55 ms de planner). Plan validé par Tony : préview agrégée au-delà
   d'un seuil, coalescence par frame (à confirmer avec ses données `[Perf]` souris
   réelle), rebuild incrémental couplé lot 7. Voir
   `docs/100_Architecture/Editor/VF-0261-Preview-Performance-Plan.md` ;
2. Vérifications de poste : smoke GUI, bug grille §10.2 (candidat : depth bias) ;
3. Mini-lot différé : skip propre des tests `EditorApp` en CI (`SKIP_RETURN_CODE`) pour
   supprimer l'annotation d'erreur cosmétique ;
4. Nettoyage différé : copies de noms de panneaux, régénérer `tests/CMakeLists.proposed.txt`,
   lot 3-bis (`BeginSaveSelectionAsStamp` + dialogue stamps), presenter des dialogues projet
   (facultatif, post-4a/4b) ;
5. Phase D ensuite : Smart Tools — gate SMART-02.5 avant SMART-03 (AR-0104).

## Dette principale

`EditorWorkspace.cpp` ≈ 9 180 lignes (God Object en résorption, −47,2 % depuis le 31/07) —
reste le lot 7 de VF-0260 (DrawScenePanel, UpdateVoxelHighlights, stroke Smart Tool),
à coupler avec le verdict PERF-01.
