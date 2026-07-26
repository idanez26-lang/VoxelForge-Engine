# AR-0103 — Smart Tool Performance Report

| Champ | Valeur |
|---|---|
| Projet | VoxelForge Studio |
| Audit | AR-01 |
| Révision | `e01b10047a818b2dd28d65c3934e1d6bf83fc7a6` |
| Build mesuré | Windows x64 Debug, MSVC 14.51, `/Od /RTC1 /MDd` |
| Règle | Mesurer uniquement, aucune optimisation |

## 1. Protocole

Une sonde temporaire non suivie a été compilée contre les bibliothèques Debug
existantes. Elle utilise les vraies classes :

- `SmartToolPlanner` ;
- `SmartToolController` et son cache de Session ;
- `SmartBrushPreviewResolver` ;
- `VoxelPencilTool` ;
- `VoxelEditHistory` ;
- transaction document/grille ;
- rebuild mesh.

Jeux d'essai :

| Taille Cube | Cellules |
|---:|---:|
| 1 | 1 |
| 4 | 64 |
| 8 | 512 |
| 16 | 4 096 |

Itérations Planner/Preview/cache :

- 2 000 pour 1 cellule ;
- 500 pour 64 ;
- 100 pour 512 ;
- 20 pour 4 096.

Le Commit est mesuré une fois par taille sur un document vide 64³, après
construction du plan et prime du mesh. Il inclut validation, création de
l'opération, historique, snapshots atomiques, mutation et rebuild.

Les allocations sont comptées via `operator new/new[]`. Les nombres incluent
les proxies STL Debug de MSVC et n'incluent pas les allocations faites
directement par `malloc`, le driver ou le GPU. Les octets résidents du plan
sont estimés à partir des capacités de vecteurs et n'incluent pas les headers
de l'allocateur ni le control block du `shared_ptr`.

## 2. Tailles statiques

| Type | Taille |
|---|---:|
| `VoxelPosition` | 12 octets |
| `SmartToolPlanCell` | 32 octets |
| `SmartBrushResult` | 256 octets |
| `SmartToolPlan` | 568 octets |
| `GhostVoxel` | 36 octets |
| `VoxelChange` | 24 octets |
| `SmartToolRequest` | 240 octets |
| `SmartToolRequestKey` | 136 octets |
| `SmartToolSessionState` | 160 octets |
| `VoxelOperation` V2 proposé | 20 octets |

La taille V2 suppose un enum explicitement sous-jacent à 8 bits et des flags
32 bits. Elle ne constitue pas un format de sérialisation.

## 3. Résultats

### 3.1 Temps moyen

| Étape | 1 cellule | 64 cellules | 512 cellules | 4 096 cellules |
|---|---:|---:|---:|---:|
| Planner miss | 20,8 µs | 81,4 µs | 552,0 µs | 4 332,6 µs |
| Preview adapter | 2,17 µs | 11,59 µs | 88,08 µs | 666,78 µs |
| Controller cache hit | 1,76 µs | 1,66 µs | 1,79 µs | 2,55 µs |
| Commit complet | 225,3 µs | 486,9 µs | 2 310,0 µs | 17 596,0 µs |

Interprétation :

- le cache du plan est O(1) et efficace ;
- le Planner est approximativement linéaire, mais son coût d'allocation
  hashée est élevé en Debug ;
- le Preview est linéaire et assez rapide, mais recrée tous ses buffers ;
- le Commit domine et atteint 17,6 ms à 4 096 cellules en Debug.

### 3.2 Allocations moyennes

| Étape | 1 cellule | 64 cellules | 512 cellules | 4 096 cellules |
|---|---:|---:|---:|---:|
| Planner miss | 46 | 110 | 559 | 4 146 |
| Preview adapter | 5 | 5 | 5 | 5 |
| Controller cache hit | 3 | 3 | 3 | 3 |
| Commit complet | 61 | 189 | 1 086 | 8 257 |

Le Planner crée un `unordered_set` par classification. Sur une scène vide,
l'ensemble Addable reçoit approximativement un nœud par cellule. Le Commit
crée des changements, une entrée d'historique, des changements dirigés, des
snapshots et les allocations liées à la mutation sparse/rebuild.

### 3.3 Octets alloués par appel

| Étape | 1 cellule | 64 cellules | 512 cellules | 4 096 cellules |
|---|---:|---:|---:|---:|
| Planner miss | 1 564 | 10 400 | 74 473 | 633 718 |
| Preview adapter | 96 | 3 120 | 24 718 | 196 750 |
| Controller cache hit | 48 | 48 | 48 | 48 |
| Commit complet | 526 851 | 549 754 | 665 492 | 1 481 760 |

Le coût fixe élevé du Commit vient principalement des snapshots complets du
document/grille nécessaires à l'atomicité, déjà identifiés par STAMP-16.

## 4. Mémoire résidente du plan

| Cellules | Octets estimés | Octets/cellule |
|---:|---:|---:|
| 1 | 693 | 693,0 |
| 64 | 5 733 | 89,6 |
| 512 | 41 573 | 81,2 |
| 4 096 | 328 293 | 80,1 |

La convergence vers environ 80 octets/cellule s'explique par :

- quatre capacités `VoxelPosition` dans `SmartBrushResult` :
  4 × 12 octets ;
- une `SmartToolPlanCell` :
  32 octets.

Même lorsque `ExistingPositions` ou `ClippedPositions` reste vide, le moteur
lui réserve la capacité du volume résolu.

Extrapolation linéaire, non benchmarkée car le Brush V1 est limité à 4 096
cellules :

| Cellules | Mémoire plan estimée |
|---:|---:|
| 32 768 | ~2,50 Mio |
| 65 536 | ~5,00 Mio |
| 262 144 | ~20,0 Mio |

Les plans Face/Fill pourraient dépasser cette estimation si diagnostics,
ancres ou topologie s'ajoutent.

## 5. Preview

Le resolver produit :

- un `GhostVoxel` de 36 octets par cellule ;
- une position affectée de 12 octets par cellule modifiée.

Le coût temporaire maximal observé est donc proche de 48 octets/cellule,
confirmé par 196 750 octets alloués pour 4 096 cellules.

`EditorWorkspace` reconstruit ensuite :

- `VoxelPlacementPreview::Positions` ;
- `AddablePositions` ;
- `OccupiedPositions`.

Selon l'action, cela ajoute statiquement jusqu'à trois copies de position, soit
jusqu'à 36 octets/cellule. Cette seconde conversion n'est pas incluse dans la
mesure de la sonde.

À 60 FPS et 4 096 cellules, le resolver seul alloue environ 11,3 Mio/s si la
fonction est rappelée chaque frame, même lorsque le plan est en cache. Ce n'est
pas une fuite : les allocations sont libérées, mais elles créent de la pression
sur l'allocateur.

## 6. Commit et Undo

Le Commit utilise un seul vecteur `VoxelChange`, puis le déplace dans
`VoxelEditOperation`. L'historique conserve cette opération sans recopier le
vecteur lors de l'insertion.

Coûts additionnels :

- relecture document + grille pour chaque cellule ;
- construction des valeurs Before pour Erase ;
- vecteur `directedChanges` de la transaction ;
- snapshot complet du `VoxelDocument` ;
- snapshot des grilles référencées ;
- mutation du document ;
- rebuild mesh ;
- contrôle post-commit par cellule.

Undo et Redo réutilisent l'opération stockée, mais recréent encore
`directedChanges` et les snapshots atomiques. La taille historique par cellule
est au minimum celle de `VoxelChange`, soit 24 octets, hors vecteur, label,
list node, document/grille temporaires et éventuelles transitions.

## 7. Copies et structures dupliquées

### Planner

1. génération de `rawPositions` ;
2. déplacement vers un buffer temporaire ;
3. reconstruction de `Positions` après clipping ;
4. `AddablePositions`/`ExistingPositions` ;
5. reconstruction de deux `unordered_set` ;
6. création de `SmartToolPlanCell` ;
7. conservation simultanée de toutes ces listes dans le plan.

### Preview

1. cellules du plan ;
2. `GhostVoxels` ;
3. `AffectedPositions` ;
4. trois vecteurs potentiels dans `VoxelPlacementPreview`.

### Commit

1. cellules du plan ;
2. `VoxelChange` ;
3. `directedChanges` ;
4. snapshots ;
5. structures du document et de la grille.

## 8. Cache

Le cache de Session est très efficace :

- temps quasi constant quelle que soit la taille ;
- 48 octets alloués en Debug ;
- même `shared_ptr<const SmartToolPlan>` rendu au Preview et au Commit.

Limites de clé :

- UUID du profil provoque un miss même si les réglages sont identiques ;
- version d'algorithme/scene/settings absente ;
- identité process-local non portable ;
- Workplane optionnel non inclus directement ;
- callback d'occupation non identifiable, compensée uniquement par la révision
  document.

## 9. Analyse de `VoxelOperation` V2

### Avantages

- représente directement Before/After ;
- permet Add/Remove/Paint/Replace avec un contrat unique ;
- conversion directe en `VoxelChange` ;
- Preview déduit son état sans `SmartBrushResult` ;
- Undo ne relit plus la palette au Commit ;
- structure compacte : 20 octets mesurés contre 32 pour la cellule actuelle ;
- sérialisable champ par champ pour macros, IA et réseau ;
- flags extensibles pour no-op, clipped, invalid, provenance ou conflit.

### Inconvénients

- les flags d'existence doivent être obligatoires, car palette 0 ne suffit pas
  à représenter une cellule vide ;
- Pick n'est pas une mutation voxel et exige un résultat de contexte séparé ;
- diagnostics, bounds et statistiques restent hors de cette structure ;
- la conversion de l'ancien plan/profils demande une migration testée ;
- une structure mémoire C++ ne peut pas être utilisée brute sur réseau ou
  disque à cause du padding et de l'endianness.

### CPU

Le Planner fera une lecture palette plutôt qu'une lecture booléenne. Cette
lecture existe déjà au Commit pour Erase/Paint ; elle est déplacée vers
l'autorité correcte. Le Commit devient une conversion linéaire simple et évite
la dérivation métier tardive.

### Mémoire

Gain direct sur la cellule : 12 octets, soit 37,5 %. Le gain réel devient bien
plus important si `SmartBrushResult` n'est plus conservé dans le plan.

### IA, macros et réseau

Le format facilite :

- inspection et explication des changements ;
- replay déterministe ;
- hash de plan ;
- validation distante ;
- fusion/conflit explicite ;
- provenance par flags/metadata.

Il faudra ajouter un schéma versionné, un ordre canonique et des identifiants
stables ; ne jamais sérialiser la mémoire brute.

## 10. Coûts principaux classés

1. snapshots document/grille du Commit ;
2. rebuild mesh ;
3. allocations sparse pendant la mutation ;
4. nœuds `unordered_set` du Planner ;
5. conservation de quatre vecteurs Brush dans le plan ;
6. cellules finales dupliquant les positions ;
7. reconstruction Preview à chaque frame ;
8. conversion supplémentaire du Workspace ;
9. validation pré/post Commit par cellule ;
10. trois allocations Debug du chemin cache hit.

## 11. Optimisations futures, non appliquées

1. plan final compact `Before/After` sans BrushResult transitoire ;
2. classification directe sans `unordered_set` de nœuds ;
3. adapter Preview conservant/réutilisant ses buffers ;
4. exposition de spans vers un buffer de Ghost stable appartenant à la Session ;
5. unification de `SmartBrushPreviewResult` et `VoxelPlacementPreview` ;
6. transaction document sparse ou copy-on-write ;
7. snapshots par sous-modèle réellement touché ;
8. rebuild incrémental mesuré ;
9. contrôle post-commit regroupé avec les résultats de transaction ;
10. benchmarks Release et gros plans Face/Fill avant optimisation.

## 12. Conclusion performance

Le Planner et son cache sont assez rapides pour Pencil V1. Le Preview ne
recalcule pas la géométrie, mais il n'est pas « zéro allocation ». Le Commit
est le coût dominant, surtout à cause de l'atomicité et du rebuild. La première
action recommandée reste architecturale — enrichir et compacter le plan —
avant toute micro-optimisation.
