# État courant du projet

| Champ | Valeur |
|---|---|
| Date de vérification | 2026-07-31 |
| Méthode | Audit Phase A (code + GitHub) + vérifications locales (git, journaux build/ctest) |
| Version | 0.1.2 — C++20, CMake ≥ 3.24, MSVC + Ninja, SDL3 + Dear ImGui (docking) |

À mettre à jour à chaque jalon. Discipline : distinguer
imaginé / validé / documenté / codé / compilé / testé / commité / poussé.

## Branches

| Branche | État |
|---|---|
| `main` | Starter kit v0.0.1, aucun code moteur |
| `feature/imgui` | Branche principale — poussée le 31/07 (`a3f3e44`) |
| `experiment/viewport-interaction-v2` | Chantier interaction V2, créée le 31/07 — ⚠️ travail en cours non commité |
| `feature/common-foundation` | Ancêtre strict de `feature/imgui` (confirmé) — à archiver/supprimer (décision Tony) |

Stash : `backup/smart-11-prototypes-before-viewport-interaction-v2` (26 fichiers).

## Carte des systèmes

| Système | État |
|---|---|
| Core (événements, app, layers, fenêtre, input) | ✅ Codé + testé |
| Renderer SDL GPU + backend ImGui + shaders HLSL | ✅ Codé + testé |
| Projets, sessions, récents, préférences | ✅ Codé + testé |
| Asset Browser, import, métadonnées, miniatures, drag & drop | ✅ Codé + testé |
| Document voxel, VOX round-trip, mesh sync, sauvegarde | ✅ Codé + testé |
| Outils voxel (crayon, gomme, peinture, ligne, boîte, sphère, fill) | ✅ Codé + testé |
| Sélection, raycast, transformations, gizmos, pivot | ✅ Codé + testé |
| Undo/redo, transactions, historique | ✅ Codé + testé |
| Workplane persistant, contraintes optionnelles | ✅ Codé + testé |
| Voxel Stamps (format, capture, bibliothèque Forge, placement) | ✅ Codé + testé (polissage restant) |
| Smart Tools (face, line, rectangle, surface, fill, strokes, mode 2D) | 🟡 Avancé — gate SMART-02.5 avant SMART-03 (AR-0104) |
| Interaction viewport V2 (pencil/sélection) | 🟠 Chantier en cours, non commité |
| Ateliers, Eldor, IA, exports Teardown/.qb, animation | ⚪ Idée future (post-v1.0) |

## Tests et build

- 176 tests CTest déclarés (74 « Smoke » exécutés le 31/07 : 100 % verts) ;
- aucun échec d'un test commité dans les journaux récents (27→31/07) ;
- build Debug MSVC 14.51 + Ninja fonctionnel (31/07) ;
- CI GitHub Actions : build + ctest complet à chaque push (dès que `ci.yml` est poussé) ;
- `tests/CMakeLists.proposed.txt` : réécriture compacte équivalente, à adopter
  après le commit du chantier V2.

## Dette principale

`EditorWorkspace.cpp` ≈ 17 400 lignes (God Object) — extraction progressive
vers services (voir Phase C de la roadmap et KNOWN-RISKS).
