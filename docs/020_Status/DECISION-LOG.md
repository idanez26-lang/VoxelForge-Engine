# Journal des décisions

Décisions structurantes, la plus récente en premier.
Règle : aucune décision n'est effective sans validation explicite de Tony.

| Date | Décision | Source |
|---|---|---|
| 2026-07-31 | Push de sauvegarde systématique : `feature/imgui` et branches actives poussées ; plus de semaine de travail non poussée | Session Cowork 31/07 |
| 2026-07-31 | Infrastructure adoptée : CI GitHub Actions (build + ctest), `.clang-format` (nouveau code uniquement), `.editorconfig`, racine rangée dans `docs/history/` | Session Cowork 31/07 |
| 2026-07-31 | Consolidation documentaire : `000_Vision` / `010_Roadmap` / `020_Status` créés ; ancienne ROADMAP archivée (pivot éditeur d'abord acté par écrit) | Audit Phase A §7.1 |
| 2026-07-31 | Chantier interaction V2 isolé sur `experiment/viewport-interaction-v2`, prototypes smart-11 sauvegardés en stash | Tony |
| 2026-07-26 | Direction Smart Tools : Controller → Session → Planner → Plan immuable conservée ; gate SMART-02.5 requis avant SMART-03 ; Face et Line modes de première classe ; **Fill est un mode, pas une action** | VF-0300, AR-0104 |
| 2026-07 (Phase 1) | Pivot « éditeur d'abord » : la v1.0 est un atelier d'édition sans IA ; génération et Intent Engine repoussés post-v1.0 | Dossier de transfert |
| Fondation | IA optionnelle ; contrôle final de l'artiste ; modularité ; non-destruction ; reproductibilité | VF-0001, VF-0002, ADR-0001..0003 |

## Décisions en attente (Tony)

1. Archiver ou supprimer `feature/common-foundation` (ancêtre strict confirmé, aucun contenu unique) ;
2. Critères de fusion de `experiment/viewport-interaction-v2` dans `feature/imgui` (quels tests verts, quel périmètre) ;
3. Adoption de `tests/CMakeLists.proposed.txt` comme `tests/CMakeLists.txt` (après commit du chantier V2) ;
4. Tag de baseline `v0.1.2` (après premier run CI complet vert) ;
5. Validation du Grand Livre et de la Roadmap v1 (les présents documents).
