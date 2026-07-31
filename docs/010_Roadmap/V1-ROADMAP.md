# Roadmap v1.0 — Éditeur d'abord

| Champ | Valeur |
|---|---|
| Statut | Remplace `docs/900_Roadmap/ROADMAP.md` (archivée) — validation Tony requise |
| Date | 2026-07-31 |
| Version actuelle | 0.1.2 — branche `feature/imgui` (+ `experiment/viewport-interaction-v2`) |

Cette roadmap acte le pivot **éditeur d'abord** : la v1.0 est un atelier
d'édition voxel professionnel complet et stable, sans IA. L'ancienne roadmap
« générateur d'abord » (Intent Engine en v0.2, édition conversationnelle en
v1.0) est archivée ; ses ambitions passent en post-v1.0.

## Phase A — Baseline vérifiée — ✅ quasi terminée

- [x] Audit de l'état réel (31/07/2026) ;
- [x] Push de sauvegarde (`feature/imgui`, `experiment/viewport-interaction-v2`) ;
- [x] Infrastructure : CI, `.clang-format`, `.editorconfig`, rangement racine ;
- [ ] Un run `ctest` complet non interrompu (la CI le fournira à chaque push) ;
- [ ] Smoke test manuel de l'éditeur ; vérif bug « grille de proximité » ; perf grand `.vox` ;
- [ ] Tag de baseline `v0.1.2` quand build + tests complets sont verts.

## Phase B — Boucle d'édition canonique

Vérifier et unifier de bout en bout :
Créer/Ouvrir → Éditer → Preview → Valider → Undo/Redo → Sauvegarder → Fermer → Recharger.
Fusionner les chemins d'édition parallèles vers la chaîne canonique
(Input → Tool Intent → Preview → Validation → Transaction → Document → History → Render Sync).
Le chantier `experiment/viewport-interaction-v2` (interaction pencil/sélection V2)
en fait partie ; critères de fusion à définir avant de continuer.

## Phase C — Dégraissage d'EditorWorkspace (continu)

`EditorWorkspace.cpp` (≈ 17 400 lignes) se vide progressivement vers les
services existants. Règles : aucun nouveau code n'y entre ; chaque chantier en
sort au moins autant de lignes qu'il en touche ; tests de non-régression
avant/après. Surveiller aussi `ViewportRenderer.cpp` et `SmartToolPlanner.cpp`.

## Phase D — Smart Tools (VF-0300)

Séquence officielle validée par AR-0104 (à ne pas réordonner) :

```text
SMART-01 ✅ -> SMART-02 ✅ -> AR-01 ✅ -> SMART-02.5 (gate requis)
  -> SMART-03 (Actions) -> SMART-04 (Modes/Scene Context)
  -> SMART-05 (Shapes/Resolver Ports) -> SMART-06 (Face) -> SMART-07 (Line)
  -> Rectangle, Fill (après stabilisation Face/Line)
```

Complets côté UX v1.0 : profils de pinceaux, favoris, options riches du
panneau Smart Tool, preview vert/orange/rouge partout.

## Phase E — Voxel Stamps : polissage

Le socle (format `.vfstamp`, capture, bibliothèque Forge, placement, rotation,
miroir, palette) est codé et testé (VF-0250/0251). Reste : finitions UX,
performance sur grandes bibliothèques, documentation utilisateur.

## v1.0 — Atelier professionnel (sans IA)

Critère de réussite : un artiste crée, édite, transforme, tamponne, sauvegarde
et recharge des modèles voxel de qualité professionnelle, avec preview exacte
et undo fiable, plus vite que dans les outils existants. Export VOX round-trip
fiable. Documentation utilisateur de base.

## Post-v1.0 (idées futures, non planifiées)

Ateliers multiples ; Maître Eldor (Support/Forge) ; barre IA et génération
(texte/image → voxel, retour du PRD) ; format natif `.vfvoxel` ; exports
Teardown, `.qb`, `.obj`/GLTF ; animation ; destruction ; cinématique.
