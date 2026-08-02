# Journal des décisions

Décisions structurantes, la plus récente en premier.
Règle : aucune décision n'est effective sans validation explicite de Tony.

| Date | Décision | Source |
|---|---|---|
| 2026-08-02 | Préview « état final exact » plafonnée par la taille du document (option A : au-delà de 50k voxels, pas de mesh exact — constante `MaximumExactPreviewDocumentVoxelCount`) ; l'exactitude sans plafond sera restaurée par le compositeur incrémental (option B, VF-0262) | Tony, session Cowork 02/08 (VF-0261) |
| 2026-08-02 | `feature/common-foundation` supprimée (ancêtre strict confirmé de `feature/imgui`, aucun contenu unique) | Tony, session Cowork 02/08 |
| 2026-08-02 | Grand Livre de la Forge et Roadmap v1 validés | Tony, session Cowork 02/08 |
| 2026-08-01 | Lot 4c clos en statu quo : `SynchronizeProjectAssets` reste dans EditorWorkspace (orchestration légitime ; pas de contrôleur à 15 références) | Tony, session Cowork 01/08 |
| 2026-08-01 | Règle de build local : Visual Studio fermé pendant tout configure/build CLI (un seul propriétaire du `binaryDir` — sinon `rules.ninja` silencieusement absent) ; CMake 4.4.1 standalone en service | Diagnostic session 01/08 (build/DIAGNOSTIC.md) |
| 2026-08-01 | Workflow de collaboration : Claude (Cowork) = code/fichiers ; Codex = exécution console sur prompts validés par Tony ; aucun commit/push sans validation explicite | Tony, session Cowork 01/08 |
| 2026-07-31 | Push de sauvegarde systématique : `feature/imgui` et branches actives poussées ; plus de semaine de travail non poussée | Session Cowork 31/07 |
| 2026-07-31 | Infrastructure adoptée : CI GitHub Actions (build + ctest), `.clang-format` (nouveau code uniquement), `.editorconfig`, racine rangée dans `docs/history/` | Session Cowork 31/07 |
| 2026-07-31 | Consolidation documentaire : `000_Vision` / `010_Roadmap` / `020_Status` créés ; ancienne ROADMAP archivée (pivot éditeur d'abord acté par écrit) | Audit Phase A §7.1 |
| 2026-07-31 | Chantier interaction V2 isolé sur `experiment/viewport-interaction-v2`, prototypes smart-11 sauvegardés en stash | Tony |
| 2026-07-26 | Direction Smart Tools : Controller → Session → Planner → Plan immuable conservée ; gate SMART-02.5 requis avant SMART-03 ; Face et Line modes de première classe ; **Fill est un mode, pas une action** | VF-0300, AR-0104 |
| 2026-07 (Phase 1) | Pivot « éditeur d'abord » : la v1.0 est un atelier d'édition sans IA ; génération et Intent Engine repoussés post-v1.0 | Dossier de transfert |
| Fondation | IA optionnelle ; contrôle final de l'artiste ; modularité ; non-destruction ; reproductibilité | VF-0001, VF-0002, ADR-0001..0003 |

## Décisions en attente (Tony)

1. Adoption de `tests/CMakeLists.proposed.txt` comme `tests/CMakeLists.txt` (à régénérer d'abord — obsolète depuis les lots 0-6).

Résolues depuis le 31/07 : fusion V2 (avance rapide dans `feature/imgui`, branche supprimée) ;
tag `v0.1.2` posé (`369277b`).
