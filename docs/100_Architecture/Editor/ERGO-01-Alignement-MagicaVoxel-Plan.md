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

### LOT 0 — Lever le plafond de 50 000 voxels

Remplacer le plafond sur la taille du **document** par une garde sur le **volume
du delta** : nombre de voxels affectés par l'opération, et nombre de chunks
touchés. Mesurer avant de choisir le seuil, ne pas l'inventer.
Prérequis : la session LATENCE-01 déjà prête (`build\latence01`), qui dira si un
autre coût se cache derrière.

### LOT 1 — Rendre la preview opaque et identique au résultat (R1)

Aujourd'hui deux mécanismes coexistent : les `GhostVoxel` **translucides** avec
alpha (`SmartPreviewEngine`, `smartBrushGhostPreview_`) et la **preview exacte**
en surimpression de chunks. R1 dit qu'il n'en faut qu'un, opaque.

- Faire de la preview exacte la source unique de « ce qui va changer ».
- Retirer le chemin translucide quand la preview exacte est disponible.
- Vérifier l'égalité preview / résultat commité, comme MagicaVoxel : c'est
  testable automatiquement, et le test d'équivalence du LOT 4c fournit déjà le
  patron.

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
