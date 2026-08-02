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

1. **PERF-02a — préview agrégée au-delà d'un seuil** — ✅ implémenté (02/08) :
   le moteur décidait déjà `AggregateBox/Sphere` au-dessus de
   `MaximumDetailedBrushPreviewVoxelCount` (256), mais la chaîne de présentation
   construisait quand même fantômes + mesh exact. Désormais, en mode agrégé :
   `SmartPreviewEngine::Build` ne construit plus `GhostVoxels`/`AffectedPositions` ;
   `UpdateVoxelHighlights` saute la composition du mesh exact et présente la
   boîte/sphère englobante via les canaux agrégés existants ; l'étiquette de
   statistiques reste exacte (gardée par `Statistics.Total`).
   Limites assumées : la gomme reste en cellules détaillées (règle produit du moteur :
   un contour agrégé prétendrait que des cellules vides sont effaçables) ; les chemins
   Face/Line/Geometry/Surface/Fill du planner forcent encore `DetailedCells`
   (candidats PERF-02a-bis si mesures défavorables).

   **Mesures après (02/08, Release `e14bd23`, session sonde in-app, modèle 16k)** :
   | Pinceau | Pire frame avant | Pire frame après | Poste dominant après |
   |---|---|---|---|
   | 3 | < 20 ms | 16,9 ms | — |
   | 16 | non mesuré | 25,5 ms | highlights 23,0 ms |
   | 64 | **258 ms** | **61,2 ms (×4,2)** | **planner (`ResolvePreview`) 57,7 ms** |

   La présentation est réglée (fantômes + mesh exact ≈ 197 ms éliminés, conformes à la
   prédiction). Le coût résiduel = **matérialisation du planner O(volume) à chaque
   nouvelle cellule survolée**, plus le multiplicateur par-événement souris réelle
   (highlights ×4 dans une même frame observé sur le segment de Tony).

   **Session souris réelle de Tony (02/08, taille 21, en dessinant, sondes PERF-01c)** —
   pire frame 258 ms : `highlights` 118,7 ms ×3, `mesh-sync` 90,7 ms ×2 (deux rebuilds
   complets du mesh dans la même frame), `hl-handoff` 7,1 ms ×3, `preview` 8,4 ms ×2,
   `vp-render` ~1 ms. Conclusions : (1) **pendant le dessin, le poste dominant est le
   rebuild complet du mesh par pas de trait** → PERF-02c prioritaire ; (2) coalescence
   ×3→×1 des highlights confirmée utile ; (3) ~35 ms/appel highlights restent non
   attribués → sous-sondes internes à poser (stroke-compose, exact-resolve, branches).
2. **PERF-02b — coalescence par frame** : multiplicateur confirmé (highlights ×4/frame
   en souris réelle) → résoudre préview/highlights au plus une fois par frame.
   Prochaine étape.
2bis. **PERF-02-planner — matérialisation paresseuse** : sur les plans agrégés, éviter
   de matérialiser les 262k cellules au survol (nécessaires seulement au commit).
   Profond : touche le planner et les invariants SMART (gate SMART-02.5) — à chiffrer
   après 02b.
3. **PERF-02c — rebuild mesh pendant le dessin** :
   - **v1 — différé pendant le flux (✅ implémenté 02/08)** : `VoxelEditTransaction`
     imbriquait un rebuild complet dans chaque transaction (garde de rollback), en
     doublon avec la sync par frame de `Draw()` qui reconstruit sur changement de
     révision. Pendant un stroke actif ou un geste V2 (`smartToolStroke_.IsActive()`
     ou `viewportInteractionV2_.OwnsPointer()`), `RebuildActiveVoxelMesh` se contente
     de réussir : plafond à 1 rebuild/frame en dessinant (mesuré ×2 auparavant).
     Compromis accepté : les pas intermédiaires perdent le rollback-si-échec-rebuild ;
     clic simple, undo et redo gardent le comportement transactionnel.
   - **v2 — rebuild incrémental par régions (chunks)** : seul vrai plafond pour très
     gros modèles (90 ms/rebuild restants) ; chantier d'architecture séparé à
     documenter (VF-0262) et valider avant toute implémentation.

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
