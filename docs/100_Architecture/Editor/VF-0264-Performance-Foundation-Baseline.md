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
édition à 512 k et au-delà, c'est encore 1,7 frame.**

**Correction du 05/08 (première rédaction erronée).** J'avais attribué ces 28 ms
à l'assemblage du mesh unique remis au renderer. C'est faux, et les mesures
elles-mêmes le démontrent : `VoxelDocumentMeshCache::Mesh()` est **paresseux**
et le banc ne l'appelle jamais — il n'appelle que `Synchronize`. Surtout, le
temps est **rigoureusement stable quand le document double** (28,36 ms à 512 k,
28,41 ms à 1 M), ce qu'un assemblage O(document) ne pourrait pas faire.

Ces 28 ms sont donc le **remaillage d'un seul chunk 32³** : 32 768 sondages de
cellule, puis la visibilité des faces à six recherches par voxel occupé, soit de
l'ordre de 400 000 recherches dans une table de hachage d'un million d'entrées.

La conséquence est structurante : **reconstruire ne serait-ce qu'un chunk
dépasse déjà le budget d'une frame.** Toute conception qui recomposerait « les
chunks touchés » serait condamnée d'avance ; il faut recomposer une région
proportionnelle au **pinceau** (VF-0265).

Mesure complémentaire du 05/08 : l'assemblage, isolé cette fois, coûte environ
**8 Mo d'allocations par édition** à un million de voxels, pour quelques
millisecondes. C'est ce qui justifie de garder la preview en rendu par chunks
plutôt qu'en mesh assemblé.

## 5. Priorités révisées par les chiffres

1. **LOT 1 — compositeur incrémental.** Le plus gros gain absolu : 1 354 ms à
   1 M, et une allocation par voxel. Rien d'autre n'approche.
2. **LOT 2 — cache de palette du placement.** 68 ms à 1 M pour un scan qui n'a
   aucune raison d'être refait à chaque frame. Correction peu risquée, gain net.
3. **LOT 3 — probeRegion : mesurer d'abord.** Le risque n'est pas visible sur le
   chemin complet. Produire la mesure régionale avant toute modification, et
   expliquer la queue de `sparse10k` en 128³.
4. **Nouveau — granularité du remaillage.** Un chunk 32³ coûte 28 ms à
   remailler sur document dense : déjà 1,7 frame pour un seul chunk. Cette
   contrainte commande l'architecture de VF-0265 (recomposer à la taille du
   pinceau, pas du chunk).

**Mise à jour du 05/08 — le chemin régional est mesuré, et il est plat.** Une
région de 11³ coûte 0,318 ms sur 15 625 voxels et 0,391 ms sur un million ; en
5³, 0,019 → 0,021 ms ; en 21³, 2,35 → 3,32 ms. Le coût suit le volume de la
région, à environ **0,25 µs par cellule**, jamais la taille du document. Sur
documents creux il est dix à cent fois moindre (0,005 ms pour 11³ dans une boîte
128³ peu peuplée). Le risque `probeRegion` est donc **non confirmé sur les deux
chemins**, et l'architecture de VF-0265 est validée par la mesure : un pinceau
9³ donnera environ 0,32 ms, contre 1 354 ms aujourd'hui.

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

## LOT 5 — gate 60 FPS (06/08/2026)

**Défaut de l'instrumentation précédente.** La sonde ne journalisait que les
frames au-delà de 20 ms. Une session pouvait donc paraître catastrophique alors
que 99 % des frames tenaient le budget, ou sembler correcte parce que le seuil
n'était jamais franchi. Aucune médiane, aucune queue, aucune proportion : rien
qui permette de dire « c'est fluide » autrement qu'au ressenti.

**Ajout.** Histogramme à pas fixe de 0,25 ms sur 0-64 ms, plus un bucket de
débordement. Borne mémoire connue (257 entiers), aucune allocation, aucun tri,
coût constant par frame. `BudgetReport()` rend `FrameCount`,
`OverBudgetCount`, `OverBudgetRatio`, `P50`, `P95`, `P99`, `Worst` et
`Saturated`. Budget = `1000/60 = 16,67 ms`.

**Limites assumées.** Un percentile est rendu comme la **borne supérieure** de
son intervalle : il est donc pessimiste d'au plus 0,25 ms. Le rang est arrondi
vers le haut (rang le plus proche), sinon un échantillon court sous-estimerait
la queue. Au-delà de 64 ms, `Saturated` est vrai et `P99` est plafonné à
`Worst` — la seule réponse honnête quand la valeur exacte n'est pas conservée.

**Émission.** Verdict écrit dans la console et dans `voxelforge-perf.log`
toutes les 5 secondes, sur TOUTES les frames et non plus seulement les lentes.
La cadence est un membre de `EditorWorkspace` et non une statique locale : les
smokes instancient plusieurs workspaces et un état partagé mélangerait leurs
mesures.

## LOT 4a — verdict de la session de sondes (06/08/2026)

Session Release, modèle 80³ rempli, crayon 1 voxel, survol rapide prolongé.
4 620 frames, dont 133 au-delà de 20 ms journalisées en détail.

| poste | médiane | p90 | max | nature |
|---|---|---|---|---|
| `hl-plan` | **13,8 ms** | 15,1 | 17,2 | **composition** de la preview exacte |
| `ho-exact` | **10,1 ms** | 13,5 | 41,3 | **envoi GPU** de cette preview |
| `ho-configure` | 0,0 | 0,0 | 0,0 | — |
| `ho-voxel` | 0,0 | 0,0 | 0,0 | — |
| `ho-transform` | 0,0 | 0,0 | 0,0 | — |
| `vp-render` | 0,5 | 0,6 | 1,9 | rendu réel |

**`ho-exact` est rigoureusement égal à `hl-handoff`** : la totalité du coût du
handoff est la branche de preview exacte. L'affirmation précédente selon laquelle
« aucun des quatre appels du handoff ne touche le GPU » était **fausse** :
`ConfigureExactPreviewChunks` appelle `UploadMesh`, donc `UploadBufferPair`, qui
crée trois objets GPU par chunk et par frame. La vérification n'avait porté que
sur `ConfigureHighlights`, et la conclusion avait été généralisée à tort.

Les deux moitiés de la preview exacte coûtent, à parts comparables, et totalisent
les ~24 ms de `highlights`. LOT 4c doit donc corriger les deux : ne recomposer
que les chunks réellement touchés, et n'en réenvoyer que ceux-là.

## LOT 5 — correctif de la métrique : tolérance de vsync

La première session a annoncé **65 % de frames hors budget** sur un geste dont la
médiane au repos était de 16,75 ms, soit 59,7 FPS. Autrement dit : le seuil
strict à 16,67 ms comptait la **gigue du vsync** comme un dépassement. La
présentation étant synchronisée sur l'écran, une frame au repos dure 16,7 ms
*par construction* ; mesurer un dépassement à cette valeur mesure l'écran, pas
notre travail.

Correctif : `BudgetToleranceMilliseconds = 1,5`. `OverBudgetCount` ne compte
que ce qui dépasse réellement, et `MissedVsyncCount` compte les intervalles de
vsync entièrement ratés — le symptôme que l'utilisateur ressent. Vérifié :
au repos 0 % au lieu de 65 %, et 50 % d'intervalles ratés sur un profil qui
alterne 16,7 et 33,3 ms. Un test échoue si quelqu'un remet un seuil strict.

**Chiffre utile de la session, relu avec la métrique corrigée** : la médiane
passe de 16,75 ms au repos à 24,5 ms pendant le survol rapide, soit **41 FPS**.
Le décrochage ressenti est donc bien réel, et vaut à peu près une frame sur deux.
