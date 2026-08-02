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

## Découpage en lots (discipline VF-0260)

- **262-0** : benchmark de référence — étendre STAMP-16 d'un scénario « édition
  incrémentale » (N poses successives, mesure du Synchronize) pour chiffrer avant/après.
- **262-1** : `VoxelMeshBuilder` par région (API `Build(grid, bounds)`) + tests
  d'équivalence (mesh complet == somme des chunks, frontières incluses).
- **262-2** : journal d'invalidation dans le document/les transactions + tests.
- **262-3** : `VoxelDocumentMeshCache` incrémental (chunks + secours complet) + tests.
- **262-4** : upload partiel côté renderer + validation visuelle sur poste.
- Verdicts : suite complète 180+, benchmark 262-0 avant/après, session sonde réelle
  (objectif : commit < 20 ms sur modèle 500k).

## Questions ouvertes pour Tony

1. Taille de chunk 32³ (défaut proposé) ou 16³ ?
2. Le futur multi-modèles/sub-models (VOX Version 150+, Sub-models: 1 aujourd'hui)
   doit-il être anticipé dans la partition, ou chunk par sub-model suffit ?
3. Priorité relative : VF-0262 avant ou après le lot 7 de VF-0260 ? (Recommandation :
   262-0/262-1 d'abord — purs et sans risque — puis lot 7, puis 262-2/4, car le lot 7
   touche les mêmes zones d'appel.)
