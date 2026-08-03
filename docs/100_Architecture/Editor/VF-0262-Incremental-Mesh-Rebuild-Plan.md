# VF-0262 — Rebuild incrémental du mesh par régions (plan, à valider par Tony)

## Problème mesuré (VF-0261, sessions des 01-02/08)

À chaque commit d'édition, `VoxelDocumentMeshCache::Synchronize` reconstruit le mesh
**entier** du document : 17 µs à 64 voxels, 73 ms à 131k, **110-120 ms mesurés en
session réelle** sur les constructions de Tony. Depuis PERF-02c, ce coût est plafonné
à une fois par frame pendant le trait, mais il reste O(taille du document) et croîtra
avec les œuvres. C'est le dernier grand plafond de la boucle d'édition.

## Objectif

Coût de remaillage proportionnel à la **zone modifiée**, pas au document :
poser un voxel dans un modèle d'un million de voxels doit coûter le remaillage
d'une région, pas du monde.

## Principe proposé

1. **Partition en chunks fixes** de 32³ (à calibrer : 16³/32³) : le document 64³ actuel
   tient dans 2×2×2 chunks ; les dimensions futures s'y étendent naturellement.
2. **Mesh par chunk** : `VoxelMeshBuilder` produit un mesh par chunk (mêmes règles de
   faces ; les faces aux frontières de chunk consultent le chunk voisin — le seul
   couplage).
3. **Invalidation partielle** : le document journalise les positions modifiées par
   révision (les transactions les connaissent déjà — `VoxelChange`) ; `Synchronize`
   ne rebâtit que les chunks touchés (+ voisins si la modification touche une
   frontière).
4. **Upload partiel** : le renderer maintient un buffer par chunk (ou un grand buffer
   avec plages par chunk) ; seuls les chunks rebâtis sont re-téléversés (upload actuel
   : coût fixe ~1 ms + volume — déjà mesuré sain).
5. **Chemin de secours** : rebuild complet conservé (chargement, undo massif,
   incohérence détectée — pari de simplicité : tout ce qui n'est pas un commit
   incrémental standard repasse par le chemin complet éprouvé).

## Invariants à préserver

- **Exactitude** : le mesh rendu reste identique bit à bit à celui du rebuild complet
  (garde de test : comparaison complet vs incrémental sur scénarios aléatoires).
- **Undo/redo** : passent par le même chemin d'invalidation (les changements inverses
  journalisent les mêmes positions) ; en cas de doute, secours complet.
- **Transactions** (VF-0261/PERF-02c) : la déferral pendant le trait reste ; le
  rebuild incrémental la rend simplement bon marché.
- `viewportState_.UpdateDocumentStatistics` et la palette : inchangés (recalcul global
  conservé, coût négligeable).

## Référence 262-0 (mesurée le 02/08, Release, poste Tony, commit `688cea6`)

| Voxels | Build complet | Région 32³ | Ratio |
|---|---|---|---|
| 15 625 | 4,70 ms | 4,65 ms | ×1,0 |
| 64 000 | 22,1 ms | 10,5 ms | ×2,1 |
| 132 651 | 58,6 ms | 15,2 ms | ×3,9 |
| 262 144 | 147,8 ms | 25,6 ms | ×5,8 |
| 493 039 | 343,3 ms | 39,1 ms | ×8,8 |
| 1 000 000 | 886,8 ms | 63,9 ms | ×13,9 |

Lecture : le build complet confirme l'O(N) catastrophique par pose ; la région 32³
gagne ×5-14 mais croît encore — parcours de collecte (`ForEachVoxel` filtré) O(N)
résiduel. **Exigence ajoutée pour 262-2/262-3 : itération régionale côté document**
(`ForEachVoxelInRegion` ou stockage par chunks) pour une région à coût ~constant
(~5 ms, indépendant du document).

## Découpage en lots (discipline VF-0260)

- **262-0** : benchmark de référence — étendre STAMP-16 d'un scénario « édition
  incrémentale » (N poses successives, mesure du Synchronize) pour chiffrer avant/après.
- **262-1** : `VoxelMeshBuilder` par région (API `Build(grid, bounds)`) + tests
  d'équivalence (mesh complet == somme des chunks, frontières incluses).
- **262-2** : journal d'invalidation dans le document/les transactions + tests.
  **✅ Implémenté (03/08)** : journal borné au niveau `VoxelDocument` (couvre
  mutations directes, transactions ET undo/redo sans instrumentation des appelants —
  les 7 sites `RecordChange()` journalisent). API : `ChangesSince(sinceRevision)`
  → positions touchées agrégées (`nullopt` = réponse impossible → secours complet ;
  vecteur vide = changements palette uniquement → **aucun remaillage nécessaire**,
  gain bonus). Bornes : 64 révisions × 4 096 positions (constantes calibrables) ;
  une mutation au-delà du plafond marque son delta « overflow » et force le secours.
  Positions agrégées tous sub-models confondus (arbitrage n° 2). Tests :
  `TestRevisionJournalChangesSince` (agrégation, palette-only, éviction de l'anneau,
  overflow, no-ops/rejets sans effet).
- **262-3** : `VoxelDocumentMeshCache` incrémental (chunks + secours complet) + tests.
  **✅ Implémenté (03/08)** :
  - *Itération régionale* (exigence 262-0) : `BuildDocumentMesh` sonde directement
    les positions de la région (clampée aux bounds) quand son volume ≤ population du
    sub-model — O(région) au lieu du parcours O(document), ordre canonique sans tri.
  - *Cache par chunks 32³* : `map` clé→mesh de chunk + mesh assemblé unique (API
    `Mesh()` inchangée, l'upload partiel attendra 262-4 ; `MeshData::Append/Reserve`
    ajoutés pour l'assemblage). `Synchronize` consomme `ChangesSince` : chunks
    touchés + voisins d'axe des positions en bord de chunk (les règles de faces ne
    regardent que les 6-voisins → pas de diagonales), rebuild régional de ces seuls
    chunks, réassemblage. Palette seule → révision adoptée, zéro remaillage.
  - *Secours complet* (rebuild de tous les chunks des bounds) si : journal muet
    (`nullopt`), identité/modèle changé, ou > `MaximumIncrementalChunkRebuilds`
    (64) chunks touchés. Échec de build → cache inchangé (garantie forte),
    révision périmée → nouvelle tentative au Synchronize suivant.
  - *Équivalence* : plafond global de faces appliqué comme au build complet ;
    garde de test `TestIncrementalSynchronizeMatchesFullBuild` (multiset de faces
    == build complet frais après édits intérieurs, en bord de chunk, retraits,
    lots composites, palette, éviction du journal, changement d'identité).
  - *Benchmark* : colonne `sync_edit_ms` ajoutée à `IncrementalEditBenchmark`
    (coût réel par édit : rebuild du chunk touché + assemblage) — à re-mesurer
    sur poste pour le verdict avant/après.
  - Concession documentée : positions journalisées tous sub-models confondus →
    sur-invalidation possible en multi-modèles (correct, jamais de faces
    manquantes) ; l'assemblage reste O(total) en memcpy (borne suivante, levée
    par l'upload partiel 262-4).

  **Mesures après (03/08, Release `b387230`, poste Tony, ctest 129/129)** :
  | Voxels | Build complet | Région 32³ | Sync par édit | Gain |
  |---|---|---|---|---|
  | 15 625 | 4,6 ms | 3,8 ms | 4,0 ms | ×1,2 |
  | 64 000 | 21,8 ms | 8,5 ms | 9,5 ms | ×2,3 |
  | 132 651 | 57,9 ms | 9,1 ms | 11,4 ms | ×5,1 |
  | 262 144 | 141,5 ms | 8,1 ms | **88,0 ms** | ×1,6 |
  | 493 039 | 347,8 ms | 8,8 ms | 26,6 ms | ×13,1 |
  | 1 000 000 | 823,0 ms | 8,9 ms | 21,0 ms | ×39,3 |

  Lecture : la région 32³ est devenue **plate** (~8,8 ms, contre 10→64 ms en
  262-0) — l'itération régionale remplit l'exigence. Le point 262 144 (cube 64³)
  est le pire cas géométrique : le voxel du benchmark {32,32,32} est au coin de
  4 chunks, or l'édit n'est qu'une recoloration → 3 remaillages de voisins
  inutiles. La pente restante (11→21 ms) est l'assemblage O(surface) (→ 262-4).

- **262-3bis** : drapeau d'occupation dans le journal. **✅ Implémenté (03/08)** :
  `ChangesSince` renvoie des `TouchedPosition{Position, OccupancyChanged}` ;
  pose/retrait → `true`, recoloration pure (`SetVoxel` sur position occupée,
  `ReplaceVoxelColor`, changes composites avec `ExistedBefore == ExistsAfter`)
  → `false`. Le cache n'invalide les chunks voisins d'une position en bord que
  si l'occupation a changé — une recoloration ne peut pas changer la visibilité
  des faces voisines. Aplati le pire cas ci-dessus à ~1 chunk et profite
  directement à l'outil peinture. Gardes : flags vérifiés dans
  `TestRevisionJournalChangesSince`, recolor-en-bord = 1 chunk dans
  `TestIncrementalSynchronizeMatchesFullBuild`.

  **Vérifié (03/08, Release `831a9c0`, ctest 129/129)** : le pire cas 262 144
  retombe de 88,0 à **19,9 ms** ; coût par édit borné à ~4-25 ms sur toutes
  les tailles (build complet équivalent : 4,6→965 ms, machine plus bruitée sur
  ce run). L'objectif « commit < 20 ms sur modèle 500k » est atteint à
  l'assemblage près (~O(surface), levé par 262-4).
- **262-4** : upload partiel côté renderer + validation visuelle sur poste.
  **✅ 4a + 4b implémentés (03/08)** :
  - *4a — exposition du cache* : `Chunks()` (chunks non vides),
    `LastSyncTouchedChunks()` (clés rebâties ou vidées par le dernier
    `Synchronize` ; vide si Unchanged/palette seule), `LastSyncWasFullRebuild()`
    (consommateurs → rafraîchissement complet). `ChunkKey` rendu public.
  - *4b — renderer par chunks* : `ViewportRenderer` tient un couple de buffers
    GPU par chunk (`UploadModelChunks` : un appel = une opération d'upload,
    `ModelUploadCount` +1 — sémantique smokes préservée) ; seuls les chunks
    touchés sont convertis (`GPUVertex`) et téléversés ; un mesh nul/vide
    retire le chunk ; N draw calls même pipeline (≤ ~64). Chemins chunké et
    mesh-entier mutuellement exclusifs (legacy `VoxelGrid` inchangé).
    `SynchronizeVoxelDocumentRendering` patch les chunks touchés si identité,
    centre du modèle et palette (bakés dans les sommets) sont inchangés ;
    sinon rafraîchissement complet — les révisions palette-seule re-bakent
    les couleurs sans remaillage. Modèle vidé → `ClearModel()` (comportement
    historique `Upload(vide)`).
  - Concessions 4b : centre baké (changement de bounds → re-upload complet,
    pas de modification de shaders) ; double upload ponctuel à l'ouverture de
    projet (chemin de chargement en mesh entier, premier Draw bascule en
    chunks).
  - **Reste 4c** : assemblage `Mesh()` paresseux (aujourd'hui encore construit
    à chaque rebuild pour les statistiques du viewport et `ReplaceDocument`) —
    dernière composante O(surface) du commit.
  - Validation : suite bloquante + smokes EditorApp sur poste (GPU requis),
    session sonde réelle (`GpuUpload` attendu ~constant), vérification
    visuelle (édits en bord de chunk, gomme, palette, undo/redo).
- Verdicts : suite complète 180+, benchmark 262-0 avant/après, session sonde réelle
  (objectif : commit < 20 ms sur modèle 500k).

## Arbitrages (validés par Tony, 02/08)

1. Taille de chunk : **32³** (constante calibrable).
2. Multi-modèles : **pas d'anticipation** — partition par sub-model, chunks à
   l'intérieur ; on généralisera si le besoin réel arrive.
3. Ordre : **262-0 puis 262-1 d'abord** (benchmark de référence + builder par région,
   purs et sans risque), puis lot 7 de VF-0260, puis 262-2/262-3/262-4.
