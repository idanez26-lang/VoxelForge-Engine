# VF-0260 — Plan de réduction d'EditorWorkspace

| Champ | Valeur |
|---|---|
| Projet | VoxelForge Studio |
| Statut | Proposition de chantier (Phase C) — validation Tony requise |
| Date | 2026-07-31 |
| Base analysée | `EditorWorkspace.cpp` 17 398 lignes / 256 méthodes ; `EditorWorkspace.h` 1 130 lignes / ~481 membres |
| Principe | Extraction progressive, un lot à la fois, tests verts avant/après chaque lot |

## 1. Constat

`EditorWorkspace` n'est pas un problème de services manquants : ~40 services
existent déjà et le fichier est surtout de la **glu d'orchestration, de l'UI
ImGui et un harnais de smoke tests embarqué**. Répartition mesurée :

| Bloc | ~Lignes | % |
|---|---|---|
| Harnais smoke/visual (`Run*SmokeStep`, `*SmokePassed`, `Run*VisualStep`) | 6 952 | 40 % |
| Panneaux UI (`DrawScenePanel` 1 536 l., `DrawMainMenuBar` 588 l., …) | 3 000 | 17 % |
| Viewport / highlights (`UpdateVoxelHighlights` 628 l., …) | 1 115 | 6 % |
| Smart tools (stroke), projets, transforms, gizmos, outils, import, stamps… | ~5 300 | 31 % |
| Divers (ctor, console, titre, raccourcis, layout, palette…) | ~1 000 | 6 % |

Aucun test n'inclut `EditorWorkspace.h` : les 119 fichiers de tests ciblent
les services déjà extraits. Les invariants délicats sont concentrés dans 3
méthodes (`DrawScenePanel`, `UpdateVoxelHighlights`, cycle de stroke Smart Tool).

## 2. Règles du chantier

1. **Aucun nouveau code n'entre dans EditorWorkspace** — tout nouveau
   comportement naît dans un service.
2. Un lot = une PR/un commit, tests verts avant/après, aucun changement de
   comportement (extraction pure).
3. Chaque lot retire du `.cpp` **et** du `.h` (membres déplacés avec leur code).
4. On ne touche au trio sensible (lot 7) qu'en dernier, quand le bruit autour
   a disparu.

## 3. Les lots, du moins risqué au plus risqué

| Lot | Contenu | ~Lignes | Cible | Tests de garde | Risque |
|---|---|---|---|---|---|
| **0** | Harnais smoke complet + ~350 membres `*Smoke*_` du header | **6 952** | Nouveau `Smoke/EditorSmokeHarness` (un fichier par domaine possible), appelé par `EditorLayer` | Les `*Tests.cpp` par domaine + le run smoke lui-même | Quasi nul (déplacement mécanique) |
| 1 | Console, titre de fenêtre, infos backend | ~45 | Nouveau `Console/EditorConsoleService` | `EditorWindowTitleTests` | Trivial |
| 2 | Layout / docking (`BuildDefaultLayout`, `DrawDockSpace`) | ~167 | `Layout/EditorLayoutPersistence` (étendre) | `LayoutStabilityTests` | Faible |
| 3 | Glu UI des Stamps (dialogue save-as-stamp, preview place/rotate/mirror) | ~298 | `StampPlacementSession`, `SaveSelectionAsStampWorkflow` + mince `StampDialogPresenter` | 16 fichiers de tests stamps | Faible |
| 4 | Gestion projet (dialogues, session, suppression, sync assets) | ~765 | `ProjectSessionService`, `ProjectDeletionService` + `ProjectDialogPresenter`, `ProjectAssetSyncService` | `ProjectSessionTests`, `WelcomeExperienceTests`, `EditorDialogTests` | Faible-moyen |
| 5 | Import + drag & drop + sortie/fermeture (actions différées) | ~574 | `ModelImportService`, `DragDropImportController` + `DestructiveActionCoordinator` | `ModelImportTests`, `DragDropImportTests`, `EditorExitRequestTests`, `SaveOnExitTests` | Moyen (ordre des frames différées à préserver) |
| 6 | Adaptateurs Apply/Cancel des outils + previews de transform | ~1 202 | `ToolManager` + `Voxel*Service`, `TransformOperationFramework`, `ConstraintEngine` (via un `VoxelEditContext` partagé à créer d'abord) | 12 tests outils + 9 tests transforms | Moyen |
| **7** | `DrawScenePanel` (1 536 l.) + `UpdateVoxelHighlights` (628 l.) + cycle stroke Smart Tool (~820 l.) | ~2 985 | `ViewportPresentation`, nouveau `ScenePanelInputRouter`, `SmartToolSession`, nouveau `HighlightCompositor` | `ViewportInteractionV2*`, `PencilInteractionV2`, 20 fichiers `SmartTool*` | **Élevé** — invariants de frame et de preview unique ; en dernier |

## 4. Trajectoire

- Après lot 0 : **17 398 → ~10 400 lignes** (−40 %) et un header lisible.
- Après lots 0-6 : **~7 400 lignes** (−57 %), sans avoir touché au chemin
  chaud viewport/Smart Tool.
- Le lot 7 se décide à ce moment-là, avec un fichier devenu lisible et une CI
  qui verrouille chaque étape.

## 5. Gate de sortie de chaque lot

- build vert + `ctest` complet vert (CI) ;
- comportement identique (smoke run inchangé) ;
- `EditorWorkspace.cpp` strictement plus petit qu'avant le lot ;
- aucun nouveau membre ajouté au header.

## 6. Décision demandée

Valider l'ordre des lots et lancer le **lot 0** (harnais smoke) — le plus gros
gain du projet pour le risque le plus faible.
