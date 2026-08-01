# État courant du projet

| Champ | Valeur |
|---|---|
| Date de vérification | 2026-08-01 |
| Méthode | Vérifications locales (git, build 761/761, ctest) + CI GitHub Actions run #11 |
| Version | 0.1.2 (tag `369277b`) — C++20, CMake ≥ 3.24, MSVC + Ninja, SDL3 + Dear ImGui (docking) |

À mettre à jour à chaque jalon. Discipline : distinguer
imaginé / validé / documenté / codé / compilé / testé / commité / poussé.

## Branches

| Branche | État |
|---|---|
| `main` | Starter kit v0.0.1, aucun code moteur |
| `feature/imgui` | **Branche de travail unique** — sommet `d383023`, CI #11 verte (01/08) |
| `experiment/viewport-interaction-v2` | Fusionnée en avance rapide dans `feature/imgui`, supprimée (locale + distante) |
| `feature/common-foundation` | Ancêtre strict de `feature/imgui` — archivage/suppression : décision Tony en attente |

## Chantier VF-0260 — dégraissage EditorWorkspace.cpp

`EditorWorkspace.cpp` : 17 398 lignes (31/07) → **10 006 lignes (01/08, −42,5 %)**.

| Lot | Contenu | État |
|---|---|---|
| 0 | Harnais smoke → `EditorWorkspaceSmoke.cpp` | ✅ Commité, CI verte |
| 1 | Console → `EditorConsoleService` (+ test) | ✅ Commité, CI verte |
| 2 | Layout dock → `EditorDockLayout` + `EditorPanelNames.h` | ✅ Commité, CI verte |
| 3 | Stamps → `StampPreviewController` (drapeau `voxelEditInProgress_` partagé par référence) | ✅ Commité, CI verte |
| 4a | Mapping pur session↔outils → `ProjectSession/ProjectSessionMapping.{h,cpp}` + test riche ; règle legacy Cube/Sphère→Pencil préservée | ✅ Commité `d383023`, CI #11 verte |
| 4b | `ProtectedProjectDeletionRoots` → `ProjectDeletionService::DefaultProtectedRoots()` (statique) + test de garde | ✅ Commité `d383023`, CI #11 verte |
| 4c | `SynchronizeProjectAssets` (~80 l., orchestration de ~15 services) | ✅ Clos par décision : **statu quo assumé** (Tony, 01/08) — orchestration légitime, un contrôleur à 15 références serait pire |
| 5 | Import / fermeture de projet | ⏭️ Prochain lot |
| 6 | Adaptateurs outils / transforms | ⚪ À venir |
| 7 | `DrawScenePanel` (1 536 l.) + `UpdateVoxelHighlights` (628 l.) + stroke Smart Tool — invariants sensibles, gardé pour la fin | ⚪ À venir |

## Tests et build

- Build Debug complet 761/761 cibles (01/08, poste local, x64) ;
- ctest ciblé lot 4 : 5/5 verts, dont le nouveau `VoxelForge.Editor.ProjectSessionMapping` ;
- CI #11 (`d383023`) : verte — tests bloquants OK ; l'étape `EditorApp` (GPU requis)
  échoue sur runner comme attendu (non bloquante) ;
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

1. Vérifications de poste : smoke GUI, bug grille §10.2 (candidat : depth bias), **benchmark
   grand `.vox` en Release** (objectiver les lags outils ressentis — build Debug suspecté) ;
2. Mini-lot différé : skip propre des tests `EditorApp` en CI (`SKIP_RETURN_CODE`) pour
   supprimer l'annotation d'erreur cosmétique ;
3. Nettoyage différé : copies de noms de panneaux, régénérer `tests/CMakeLists.proposed.txt`,
   lot 3-bis (`BeginSaveSelectionAsStamp` + dialogue stamps), presenter des dialogues projet
   (facultatif, post-4a/4b) ;
4. Phase D ensuite : Smart Tools — gate SMART-02.5 avant SMART-03 (AR-0104).

## Dette principale

`EditorWorkspace.cpp` ≈ 10 000 lignes (God Object en résorption, −42,5 % depuis le 31/07) —
poursuivre les lots 5→7 de VF-0260.
