# ERGO-01 — Alignement de l'ergonomie sur MagicaVoxel

Plan de chantier, **non validé**, rédigé le 06/08/2026.
Règles de référence : `docs/020_Status/ERGONOMIE-BACKLOG.md`, observées sur
MagicaVoxel 0.99.7.2.

Directive de Tony : **tous** les outils, création comme réutilisation, adoptent
l'ergonomie de MagicaVoxel. L'écart à cette référence est le défaut.

## Le verrou à lever d'abord

`MaximumExactPreviewDocumentVoxelCount = 50'000`
(`EditorWorkspaceHighlights.cpp`) désactive la preview exacte au-delà de 50 000
voxels. Or **R1 exige que la preview soit toujours le résultat**. Un 64³ plein
fait 262 144 voxels : avec ce plafond, l'outil n'a aucune preview conforme dès
qu'un modèle devient sérieux — et c'est exactement la taille où Tony travaille.

Ce plafond avait été posé quand une composition coûtait 75 à 144 ms. Après
LOT 4c elle coûte **0,2 ms** et l'envoi GPU **0,0 ms**, mesurés. Le plafond n'a
plus de justification de coût ; il doit être remplacé par une garde sur le
**volume réellement modifié**, qui est la seule quantité dont le coût dépend
maintenant.

**Sans cette levée, aucun autre lot n'a de sens.** C'est le lot 0.

## Ordre des lots

L'ordre suit le critère retenu : **pénibilité du geste multipliée par sa
fréquence**, pas l'ampleur du symptôme. Le crayon et le survol passent donc
devant la rotation, qu'on utilise cent fois moins souvent.

### LOT 0 — Lever le plafond de 50 000 voxels — **FAIT, mesuré**

Mesure du 06/08/2026, `build\ergo01-lot0.csv`, phases `preview_cached_cold/warm`
avec le cache d'overrides du LOT 4c et **sans** assemblage monolithique, comme
l'éditeur réel.

**Deux hypothèses de départ ont été réfutées par la mesure.**

*Réfutation 1 — le plancher ne grimpe pas avec le document, il descend.* Pour un
delta de 27 voxels : un million de voxels denses coûte **0,053 ms** à froid, un
25³ en coûte **0,484**, et dix mille voxels épars en 64³ **2,283**. Le document
le plus gros est le moins cher.

*Raison, qui n'avait pas été anticipée* : le coût est dominé par le nombre de
**faces du chunk touché**, borné par le chunk (32³), et non par la taille du
document. Un chunk au cœur d'un modèle dense n'a presque aucune face — tout est
masqué par les voisins ; des voxels épars en exposent le maximum. L'anomalie
`sparse10k`, jusqu'ici différée, est donc le cas dimensionnant.

*Réfutation 2 — le plafond ne mesurait pas seulement mal, il mesurait à
l'envers.* À 50 000 voxels de document, il **laissait passer** dix mille voxels
épars (2,3 ms) et **bloquait** un million de voxels denses (0,05 ms).

**Courbe retenue** — p95 du pire scénario, à froid :

| delta | froid | chaud |
|---|---|---|
| 27 | 4,00 ms | 0,59 ms |
| 729 | 4,67 | 0,96 |
| 4 913 | 6,69 | 2,22 |
| 35 937 | **31,92** | **18,91** |

Basculement entre 5 000 et 36 000 voxels de delta.

**Décision** : `MaximumExactPreviewDeltaVoxelCount = 8'192`, appliqué au nombre
de changements accumulés pendant un trait et au nombre de cellules du plan au
survol. Le pire cas mesuré reste alors autour de 10 ms, ce qui laisse la marge du
reste de la frame. Les gros pinceaux sont de toute façon déjà exclus en amont par
`aggregateSmartPreview`.

**Conséquence à surveiller** : la preview exacte va désormais tourner sur des
documents où elle ne tournait **jamais** — c'est tout l'objet du lot, mais cela
signifie que les 51 smokes exercent des chemins neufs.

**VALIDÉ visuellement par Tony le 06/08/2026**, binaire `build\ergo01`. Et le lot
a corrigé un **second défaut, non anticipé**, que Tony a signalé juste avant de
tester : sur un 64³, les voxels posés en maintenant le clic n'apparaissaient
**qu'au relâchement**.

Mécanisme : pendant un trait, les voxels ne sont pas écrits dans le document — ils
s'accumulent en attente dans `SmartToolStroke`, et c'est la **preview exacte** qui
les affiche ; le document n'est muté qu'au commit. L'ancien plafond rendait
`exactPreviewAffordable` toujours faux sur un 64³, la branche appelait
`smartToolExactPreviewCache_.Clear()`, et il n'y avait donc **rien à voir** avant
le relâchement. La garde sur le delta rétablit l'affichage en direct.

**Leçon de méthode, à retenir pour toute la mission.** Nos 149 tests vérifient
l'état du document après commit ; les 51 smokes vérifient que l'éditeur ne casse
pas. **Aucun ne vérifie qu'on voit quelque chose pendant le geste.** Ce défaut
était donc invisible à toute la suite de tests, et seule la main de Tony pouvait
le trouver. Les lots suivants portent tous sur ce qui est *affiché* : leur
validation sera visuelle par construction, et il ne faut pas s'attendre à ce que
les tests la remplacent.

### LOT 1 — **prémisse réfutée**, remplacé par deux correctifs réels

Le lot était écrit ainsi : « deux mécanismes coexistent, les `GhostVoxel`
translucides et la preview exacte ; il n'en faut qu'un, opaque ». **La lecture du
code réfute cette prémisse sur trois points.**

1. Les canaux `brushPreview`, `brushOccupiedPreview`, `brushAggregatePreview` et
   `brushAggregateSpherePreview` **ne sont pas translucides**. Ce sont des
   contours filaires en alpha 1,0 (`ViewportRenderer.cpp:378-385`), tirés avec
   `pipeline_`, dont le blending n'est **jamais** activé.
2. `smartBrushPreview` reçoit un alpha de **1,0**, pas 0,5
   (`SmartPreviewEngine.cpp:100-104`, verrouillé par
   `SmartPreviewEngineTests.cpp:85`). Les valeurs 0,5 vivent dans
   `SmartBrushPreviewResolver` et `LegacySmartBrushPreview`, qu'**aucun chemin
   d'exécution n'appelle** — seulement des tests.
3. Les cinq canaux sont **déjà mutuellement exclusifs** avec la preview exacte :
   `aggregateSmartPreview` et `faceAddPlanGhostPresentation` laissent tous deux
   `exactSmartToolPreview` à `nullptr`. **Aucune double présentation n'existe.**

La seule translucidité réelle restante est celle des tampons de stamps
(α = 0,52, `Preview/VoxelPreview.cpp:75`), qui n'entre pas par
`ConfigureHighlights` et ne concerne pas le crayon. **Hors périmètre.**

Le lot 1 tel qu'écrit était donc un **no-op**. Il est remplacé par les deux
défauts réels que la cartographie a mis au jour.

#### LOT 1a — la suppression des surlignages ignorait le prédicat de rendu

Le bloc qui efface `hovered`, `selected` et les deux bornes de sélection était
conditionné au **calcul** de la preview exacte, pas à son **rendu** : il manquait
`ShouldRenderExactPreviewGeometry(previewSubject, strokeActive)`. Conséquence
visible : sur un crayon un voxel **hors trait**, la preview était calculée, non
rendue, et le surlignage de survol supprimé quand même — l'utilisateur perdait
son repère sans rien gagner. La condition est désormais exactement celle du bloc
de présentation.

#### LOT 1b — le trou de présentation au-delà du plafond de delta

En mode `DetailedCells`, si le delta accumulé dépasse
`MaximumExactPreviewDeltaVoxelCount`, la preview exacte est abandonnée — et
**rien ne la remplaçait** : ni maillage exact, ni fantômes (vidés en amont), ni
agrégat (posé uniquement pour les modes agrégés). Sur un trait long,
l'utilisateur **dessinait à l'aveugle**.

C'est le seul endroit où la règle R1 était violée **par défaut** plutôt que par
excès. On présente désormais au moins l'enveloppe du plan, ce qui respecte
« montrer ce qui va changer » à la précision près.

Deux gardes explicites, toutes deux trouvées en relisant le correctif :
`SmartBrushBounds` ne portant pas de drapeau de validité, la boîte est vérifiée
composante par composante ; et le cas **Face + Add pendant un trait** est exclu,
car il présente déjà des fantômes par cellule — y ajouter l'enveloppe serait la
double présentation que la règle interdit.

#### Ce qu'il reste à faire pour R1, et qui n'est pas dans ce lot

Rien sur la translucidité : il n'y en a pas. La vérification automatique
« preview identique au résultat commité » reste souhaitable et le test
d'équivalence du LOT 4c en fournit le patron, mais elle porte sur le
**compositeur**, déjà couvert, et non sur la présentation.

### LOT 2 — L'ancre rouge d'un voxel (R4)

Marqueur opaque d'un voxel, **couleur d'interface fixe**, dessiné **par-dessus**
la preview : voxel sous le curseur, ou coin mobile d'un glissement.
Ne pas centrer sur les brosses de taille paire — la référence ne le fait pas.
Accepter qu'à taille 1 l'ancre masque la preview : c'est le comportement voulu.

### LOT 3 — Pas de preview quand rien ne change (R2)

Si l'opération n'affecte aucun voxel, ne rien dessiner. La preview devient alors
un **retour de validité** : son absence dit que le clic ne fera rien. Aujourd'hui
nous affichons un fantôme même là où la pose échouera.

Contient aussi : ne dessiner que les **faces visibles** du delta. Une brosse
sphérique 3D sur un mur plat doit donner un disque plat.

### LOT 4 — Sélection : quadrillage blanc par voxel (R4)

Contour fin pendant le tracé, puis quadrillage blanc voxel par voxel sur les
faces sélectionnées, couleurs d'origine visibles dessous. Couleur d'interface,
paramétrable.

### LOT 5 — Déplacement : opaque à la nouvelle place, vrai trou à l'ancienne

Le comportement de référence en entier : matière opaque dans ses vraies couleurs
à la destination, **trou réel** à l'origine cerné d'un contour, snap au voxel,
verrouillage sur un axe (deux avec Ctrl+Shift), réversibilité en direct à offset
nul.

### LOT 6 — Rotation : angle libre au degré, accroche sur Shift

Gizmo trackball centré sur la sélection, plus grand qu'elle : cercle extérieur
aligné écran, cercles d'axes colorés, lentille qui passe au rouge sur l'axe
actif. Résultat **revoxelisé en direct et opaque**.

Ceci **valide la conception de l'anneau à 45°** différée jusqu'ici : la référence
fait de l'angle libre avec accroche optionnelle, et non des crans imposés.

### LOT 7 — Report numérique de l'état (R5)

Coordonnées du voxel survolé, offset `x/y/z` pendant un déplacement, `angle:`
pendant une rotation. Simple, peu coûteux, et ça enlève beaucoup d'incertitude
au geste.

## Arbitrages rendus

**R3 — gomme et outils par région : aucune preview.** Tranché par Tony le
06/08/2026 : on fait comme MagicaVoxel. Seule l'ancre est affichée. Le lot 3
inclut donc le **retrait** de toute preview de suppression existante, et non son
amélioration.

## Ce que la mesure de latence a changé au plan

Session LATENCE-01 du 06/08/2026, 3 879 frames :
**`retard surlignage 0.00 frames en moyenne, max 0`**.

La coalescence du lot 7b est **innocente** : elle ne diffère jamais rien. Elle ne
doit pas être retirée. Et la sonde mesurait le report de la *résolution*, alors
que la latence ressentie est l'**âge de l'échantillon de pointeur** plus la
latence de **présentation** — deux quantités différentes.

Explication qui reste, et qui colle à tout l'observé : le curseur visible est le
**curseur matériel du système**, qui n'attend ni notre frame ni le vsync. Notre
surlignage est dessiné dans une frame présentée à la synchronisation suivante,
soit deux à trois frames plus tard. Aucun compteur ne le montre parce que rien
n'est lent : c'est la chaîne d'affichage. Cela explique que le symptôme soit
**identique en 32³ et en 64³**, qu'il **survive** à la suppression de 24 ms de
travail, et qu'il se voie surtout au survol **rapide**.

MagicaVoxel subit la même physique. La différence est ce qu'il **affiche** : une
petite face opaque, discrète, dont le retard se remarque à peine, là où notre gros
volume fantôme translucide le rend criant.

**Conséquence pour le plan : l'alignement EST probablement le correctif de la
désynchronisation ressentie**, pas un chantier qui s'ajoute à lui. Le LOT 2
(ancre d'un voxel) devient donc prioritaire au même titre que le LOT 1, et sa
validation est visuelle et tactile — c'est Tony qui dira si ça se sent, aucune
sonde ne le dira.

## Ce qui reste à observer avant de planifier plus loin

Le panneau Edit de MagicaVoxel n'a pas pu être déplié à la résolution
disponible : on ignore lesquelles de ses opérations offrent un aperçu. Non
observés également : le mode Face (extrusion), Pattern, Voxel Shader, Region
Select, les previews de Scale et de Wrap. Aucun lot ne doit être écrit sur ces
zones avant observation.

## Rappel de méthode, gagné à la dure

Régler la couleur courante sur une **teinte contrastée** avant d'observer une
preview. Une première lecture faite en couleur par défaut avait conclu que
MagicaVoxel ne montrait pas le résultat — conclusion fausse, qui aurait conduit à
jeter PERF-02a, VF-0262, VF-0265 et LOT 4c. L'ancre rouge masquait la preview.
