# VF-0261 — PERF-02 : performance de la préview outils (plan)

Objectif (exigence Tony, 01/08) : **latence d'édition stable quelle que soit la taille du
modèle ET la taille de l'outil**.

## Mesures (01/08, Release, poste Tony)

### Benchmark STAMP-16 (`build/windows-release/perf-lags.csv`)
| Taille | Mesh rebuild (Execute) | Upload GPU |
|---|---|---|
| 64 voxels | 17 µs | 1,09 ms (coût fixe) |
| 4 096 | 1,2 ms | 1,9 ms |
| 131 072 | 73 ms | 4,2 ms |
| 262 144 | 166 ms | 5,2 ms |

### Sonde de frame PERF-01 (`EditorFrameProbe`, commits `ef412d6`/`209c1e0`)
Session pilotée in-app, modèle 16 306 voxels, pinceau Cube :
- Taille 3 : tous postes < 1 ms, aucune frame lente.
- **Taille 64 : frame de 258 ms — `highlights` 254,7 ms ×1 (dont `preview`/planner
  54,6 ms), `scene-panel` 255,1 ms, `mesh-sync` 0 ms.**

Verdict : le coût est dans `UpdateVoxelHighlights` et croît avec le **volume de la
préview**, pas avec le modèle. ~55 ms de planner (`ResolvePreview`) + ~200 ms de
présentation : `SmartPreviewEngine` (liste de `GhostVoxel`), boucle par-fantôme
(3 `push_back` × 262 144), `SmartToolExactPreviewComposer` (mesh exact de la région).

Question ouverte : le lag ressenti à petite taille (souris réelle haute fréquence) n'est
pas reproduit par la sonde en événements synthétiques — données `[Perf]` d'une session
manuelle Tony avec la sonde PERF-01b attendues pour trancher (multiplicateur
par-événement vs autre poste).

## Plan de correction (validé par Tony, 01/08)

1. **PERF-02a — préview agrégée au-delà d'un seuil** : au-dessus de N voxels affectés
   (~4 096, constante à calibrer), présenter la boîte/sphère englobante (le renderer
   supporte déjà `brushAggregatePreview`/`RenderPlan`) au lieu des fantômes individuels
   et du mesh exact. Coût borné ~constant → stabilité en taille d'outil.
2. **PERF-02b — coalescence par frame** (si les données Tony confirment le multiplicateur
   par-événement) : résoudre préview/highlights au plus une fois par frame.
3. **PERF-02c — rebuild mesh incrémental** (gros modèles, au commit) : 73-166 ms mesurés ;
   chantier séparé, à coupler au lot 7.

## Invariants à préserver (lus dans le code, 01/08)

- Règle « une seule préview » : Pencil V2 actif ⇒ présentation legacy dormante
  (`pencilV2ToolActive`, commentaire dans `UpdateVoxelHighlights`).
- Pendant un stroke actif, le plan accepté par `ContinueSmartToolStroke` est réutilisé —
  ne jamais replanifier depuis le hover brut (commentaire « would make the cursor outrun
  the actual stroke »).
- Caches existants à respecter : `smartPreviewCache_`, `smartToolExactPreviewCache_`,
  `smartToolStrokePreviewMesh_` (rebuild conditionné par PlanId/Revision/StrokeRevision),
  `pencilPreviewCacheValid_`, `paintPreviewCacheValid_`.
- `voxelEditInProgress_` partagé par référence avec `StampPreviewController` (lot 3).

## Gardes de validation

- Sonde `EditorFrameProbe` : re-mesurer taille 3/16/64 avant/après (objectif : worst
  frame < 20 ms à toutes tailles hors chargement).
- Suite bloquante complète + smokes EditorApp sur poste (TMP redirigé vers E:).
- Vérification visuelle : préview agrégée lisible (boîte englobante + statistiques),
  bascule seuil sans à-coup.
