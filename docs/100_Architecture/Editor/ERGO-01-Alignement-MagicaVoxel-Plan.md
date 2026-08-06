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

## Ce qui demande une décision de Tony

**R3 — l'ajout se prévisualise, le retrait non.** MagicaVoxel ne montre rien
pour la gomme ni pour les outils par région. C'est autant une limite de son
moteur qu'un choix d'ergonomie. Copier ce comportement est défendable au nom de
la cohérence ; prévisualiser une suppression serait un progrès réel. **À ne pas
trancher par mimétisme.**

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
