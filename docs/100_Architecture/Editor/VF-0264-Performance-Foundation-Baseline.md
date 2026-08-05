# VF-0264 — Fondation performance : mesures de base (LOT 0)

Date : 2026-08-05. Branche `feature/imgui`, sommet `071a615`.
Banc : `benchmarks/PerformanceFoundationBaseline.cpp`, **Release**, MSVC 14.51,
dossier `build/perf-release`. Données brutes : `build/perf-baseline.csv`.
Budget de référence : **16,67 ms** (une frame à 60 FPS).

Chaque chemin mesuré est une fonction statique sans interface : ni GPU, ni ImGui,
ni boucle de frame. Les chiffres ci-dessous sont donc des coûts **par appel**,
pas des temps de frame.

## 1. Compositeur de preview exacte — problème confirmé

`SmartToolExactPreviewComposer::Compose`, deux tailles d'empreinte (27 cellules
et 729 cellules) sur le même document.

| Voxels du document | p50 empreinte 27 | p50 empreinte 729 | Allocations | Octets |
|---|---|---|---|---|
| 15 625 | 7,24 ms | 7,84 ms | 15 713 | 1,5 Mo |
| 50 653 | 29,64 ms | 28,56 ms | 50 720 | 4,6 Mo |
| 103 823 | 82,43 ms | 75,38 ms | 103 890 | 8,9 Mo |
| 512 000 | 536,0 ms | 534,2 ms | 512 059 | 38,2 Mo |
| 1 000 000 | 1 354,0 ms | 1 272,6 ms | 1 000 059 | 72,9 Mo |

Deux faits, et ils suffisent :

1. **L'empreinte du pinceau n'a aucune influence.** Multiplier les cellules
   modifiées par 27 ne change pas le coût — il est parfois même plus bas, dans
   le bruit. Le coût suit exclusivement la taille du document.
2. **Une allocation par voxel du document.** 1 000 059 allocations pour un
   million de voxels : c'est la copie privée du document (un nœud de table de
   hachage par voxel), exactement ce que l'en-tête du compositeur décrit.

Dès 50 000 voxels, **100 % des exécutions dépassent le budget de frame**. Le
plafond `MaximumExactPreviewDocumentVoxelCount = 50 000` n'était donc pas
prudent : il était déjà généreux — à 50 k on est à 29,6 ms, soit 1,8 frame.

## 2. Scan de palette du Stamp Placement — problème confirmé

`StampPlacementPlanner::Build`, Stamp constant de 8³ (512 voxels), même
transform, sur des documents de tailles croissantes.

| Voxels du document | p50 | p95 | Allocations |
|---|---|---|---|
| 15 625 | 0,141 ms | 0,147 ms | 10 |
| 50 653 | 0,378 ms | 1,560 ms | 10 |
| 103 823 | 0,954 ms | 4,649 ms | 10 |
| 512 000 | **30,39 ms** | 32,70 ms | 10 |
| 1 000 000 | **68,01 ms** | 73,05 ms | 10 |

Le nombre d'allocations est **constant** (10, 45 Ko) : ce n'est pas un problème
de mémoire, c'est un parcours. Le Stamp ne change jamais ; seul le document
grandit. La progression est pire que linéaire entre 104 k et 512 k (5× les
voxels, 32× le temps), ce qui trahit un parcours de table de hachage devenu
hostile au cache.

À un million de voxels, **une seule reconstruction de preview coûte 4,4 frames**
— et le Preview Assist peut la doubler.

## 3. Heuristique probeRegion — non confirmé par ces mesures

L'invariant redouté était : « une boîte 256³ contenant 1 000 voxels ne doit pas
coûter comme 16,7 millions de cellules ». Les mesures disent qu'il est **déjà
respecté** sur le chemin mesuré.

| Matière | Boîte 64³ | Boîte 128³ | Boîte 256³ |
|---|---|---|---|
| 1 000 voxels | 0,610 ms | 0,548 ms | 0,453 ms |
| 10 000 voxels | 7,713 ms | 9,103 ms | 7,596 ms |

Le coût suit la **matière**, pas le volume — il décroît même légèrement quand la
boîte grandit à matière constante. Aucun dépassement de budget sur les scènes à
1 000 voxels, quelle que soit la taille de la boîte.

**Réserve honnête** : ce banc mesure `VoxelMeshBuilder::Build(document)`, le
chemin complet. Il n'exerce pas le chemin **régional** (`Build(document, min,
max)`) sur documents creux, qui est justement celui où `probeRegion` choisit.
Le risque n'est donc ni confirmé ni écarté : il est **hors de portée de cette
mesure**. Avant de toucher à l'heuristique, le LOT 3 doit d'abord produire une
mesure du chemin régional — sinon on optimiserait à l'aveugle.

Une anomalie à expliquer au passage : `sparse10k` en boîte 128³ montre p50
9,1 ms mais p95 21,8 ms et pire cas 40,6 ms — une queue à 4,5×, absente des
autres scènes creuses. Deux exécutions sur vingt et une dépassent le budget.

## 4. Rebuild incrémental (VF-0262) — tient, mais plafonne

| Voxels | Rebuild complet | Édition + Synchronize | Rapport |
|---|---|---|---|
| 15 625 | 5,01 ms | 4,30 ms | 1,2× |
| 103 823 | 66,89 ms | 17,02 ms | 3,9× |
| 512 000 | 443,9 ms | 28,36 ms | 15,7× |
| 1 000 000 | 1 075,6 ms | 28,41 ms | **37,9×** |

Le gain de VF-0262 est confirmé et s'amplifie avec la taille. Mais **28 ms par
édition à 512 k et au-delà, c'est encore 1,7 frame** : le coût ne vient plus du
remaillage des chunks touchés (constant, il ne dépend pas de la taille) mais de
l'assemblage du mesh unique remis au renderer, qui reste O(document). C'est un
poste à traiter, probablement avec le LOT 4.

## 5. Priorités révisées par les chiffres

1. **LOT 1 — compositeur incrémental.** Le plus gros gain absolu : 1 354 ms à
   1 M, et une allocation par voxel. Rien d'autre n'approche.
2. **LOT 2 — cache de palette du placement.** 68 ms à 1 M pour un scan qui n'a
   aucune raison d'être refait à chaque frame. Correction peu risquée, gain net.
3. **LOT 3 — probeRegion : mesurer d'abord.** Le risque n'est pas visible sur le
   chemin complet. Produire la mesure régionale avant toute modification, et
   expliquer la queue de `sparse10k` en 128³.
4. **Nouveau — assemblage du mesh.** L'édition incrémentale plafonne à 28 ms au
   delà de 512 k à cause de l'assemblage final. À rattacher au LOT 4.

## 6. Ce que ce banc ne mesure pas

Les percentiles **de frame** exigés par le gate 60 FPS (§11 de la mission) ne
peuvent pas venir d'ici : ce sont des coûts par appel, hors boucle de rendu.
Ils viendront de la sonde applicative (`voxelforge-perf.log`) pendant une
session réelle, au LOT 5.

## 7. Note d'environnement

Le dossier `build/windows-release` généré par preset ne reçoit jamais son
`CMakeFiles/rules.ninja` : `cmake` annonce pourtant la génération réussie et
sort en code 0, et `ninja` échoue ensuite sur une règle inconnue. Un dossier
généré à la main (`cmake -S . -B build/perf-release -G Ninja
-DCMAKE_BUILD_TYPE=Release -DVF_BUILD_BENCHMARKS=ON`) fonctionne. La cause est
dans ce dossier de build, pas dans l'environnement.

Rappel : `VF_BUILD_BENCHMARKS` vaut **OFF** par défaut ; toute génération à la
main doit l'activer explicitement.

Le disque système `C:` n'a que **2,6 Go libres sur 232**. À surveiller : une
chaîne MSVC écrit des fichiers temporaires système même quand le projet est sur
un autre volume.
