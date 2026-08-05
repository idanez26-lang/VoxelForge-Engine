# VF-0265 — Compositeur de preview exacte incrémental

**État : plan à valider par Tony. Aucune ligne de code de production écrite.**
Date : 2026-08-05. Branche `feature/imgui`. Fait suite à VF-0264 (mesures) et
remplace l'« option B » esquissée dans VF-0261.

## 1. Le problème, chiffré

`SmartToolExactPreviewComposer::Compose` compose la preview « état final
exact ». Mesures Release (VF-0264) :

| Voxels du document | p50 | Allocations |
|---|---|---|
| 15 625 | 7,2 ms | 15 713 |
| 50 653 | 29,6 ms | 50 720 |
| 512 000 | 536 ms | 512 059 |
| 1 000 000 | 1 354 ms | 1 000 059 |

La taille du pinceau n'a **aucune** influence : 27 cellules ou 729 cellules,
même temps. Le coût suit exclusivement la taille du document.

La cause est en une ligne — `SmartToolExactPreviewComposer.cpp:52` :
`VoxelDocument finalDocument = document;`. Le stockage voxel est une table de
hachage : la copie alloue **un nœud par voxel**. Puis `VoxelMeshBuilder::Build`
remaille l'intégralité du document.

Le plafond `MaximumExactPreviewDocumentVoxelCount = 50 000` désactive
aujourd'hui la preview exacte au-delà. Ce n'est pas une précaution : à 50 k on
est déjà à 1,8 frame.

## 2. La contrainte qui commande l'architecture

Un chunk 32³ coûte **28 ms** à remailler sur document dense (VF-0264 §4
corrigé). C'est 1,7 frame pour **un seul** chunk.

Donc : recomposer « les chunks touchés » — la lecture naturelle du problème —
est condamné d'avance. Il faut recomposer une région proportionnelle au
**pinceau**, pas au chunk.

## 3. Le théorème de localité

Soit `O` l'ensemble des cellules changées par le plan, `B` leur boîte
englobante, et `B⁺` cette boîte dilatée d'un voxel sur chaque axe.

> Pour tout voxel hors de `B⁺`, les faces visibles sont identiques avant et
> après application de `O`.

Démonstration : un voxel hors de `B⁺` est à distance ≥ 2 de `B`. Il n'a donc
pas changé, et aucun de ses six voisins n'a changé — or la visibilité d'une
face ne consulte que ces six voisins, jamais les diagonales.

D'où l'identité exploitable :

```
Mesh(document ⊕ O) = [ Mesh(document) privé des faces des voxels de B⁺ ]
                     ⊎ Build(document ⊕ O, B⁺)
```

Et `Mesh(document)` est **déjà disponible, chunk par chunk**, dans
`VoxelDocumentMeshCache::Chunks()` à la même révision.

Coût par mouvement de pointeur, pinceau 9³ : la région sondée vaut exactement
`|B⁺|` = 11³ = 1 331 cellules, soit **environ 2 ms — et plat quelle que soit la
taille du document**.

## 4. Décisions d'architecture

### 4.1 Comment mailler l'état final sans copier le document

Trois voies étaient possibles.

**Retenue — une vue « document + changements en attente ».** Le constructeur de
mesh n'a besoin que de trois primitives sur sa source : `HasVoxel` pour la
visibilité, `GetVoxel` pour la collecte, `Bounds`/`VoxelCount` pour
l'heuristique et le clipping. Une vue en lecture seule qui intercale les
changements devant le sous-modèle les satisfait toutes, en mémoire
proportionnelle aux seuls changements. Elle répond aussi **hors région**, ce
qui rend la visibilité aux frontières exacte par construction : même prédicat,
même builder, même code de face que le commit.

**Rejetée — ouvrir le remaillage privé du cache de mesh.** Il maille le
document, pas « document + changements » : il faudrait de toute façon lui
passer une vue. Et il **mute l'état privé** du cache : l'ouvrir donnerait à la
preview le pouvoir de polluer la structure qui décrit le document *sans* le
plan. Sa granularité est par ailleurs celle qui coûte 28 ms.

**Rejetée — copier seulement les chunks touchés.** Huit chunks plus leur
pourtour font 262 000 voxels, du même ordre que ce qu'on veut supprimer. Pire :
il faudrait fabriquer un document partiel, donc décider de ses dimensions — or
la validation des changements teste l'appartenance aux dimensions. Un
sous-document aux bornes réduites **refuserait des changements que le commit
accepte**. Deuxième source de vérité sur la géométrie : exclu.

Ce qui *doit* être extrait du cache de mesh, en revanche, c'est
**l'arithmétique de chunk** (taille d'arête, clé, position → clé, bornes d'un
chunk). Sans mise en commun, la preview et le modèle divergeront un jour sur la
définition d'un chunk — trous ou faces doublées, silencieusement.

### 4.2 Le piège du clipping

Le builder clippe la région demandée aux bornes du sous-modèle. Si les
changements **ajoutent** des voxels hors des bornes actuelles, le clipping les
supprimerait. La vue doit donc exposer `Bounds() = union(bornes du modèle,
boîte des positions ajoutées)`. Le rétrécissement après suppression est sans
danger : un sur-ensemble reste correct.

C'est le bug le plus probable de tout le chantier ; il aura son test dédié.

### 4.3 Où vivent les chunks de preview

Le résultat de composition devient une **liste d'overrides** : uniquement les
chunks qui diffèrent du modèle. Un override **vide** signifie « ce chunk
n'affiche rien tant que la preview est active » — cas de l'effacement du
dernier voxel. Les chunks non touchés ne sont ni copiés ni recalculés : ils
restent ceux du modèle.

Le cache de preview reste **une instance, une entrée**, détenue par
`EditorWorkspace`. Avec une recomposition à ~2 ms, un cache multi-entrées
serait de la complexité sans contrepartie. Il lit les chunks du modèle par
pointeur constant et ne les écrit jamais.

### 4.4 Rendu chunké, et ce n'est pas optionnel

Garder un maillage assemblé reviendrait à remplacer un compositeur O(document)
par un **uploader** O(document) : assemblage de toutes les faces, puis
reconstruction d'un tampon de sommets avec une résolution de palette par
sommet, puis envoi de plusieurs dizaines de mégaoctets — à chaque mouvement de
souris. Gain apparent au banc CPU, gain nul en frame réelle.

En chunké différentiel, l'envoi porte sur au plus huit petits chunks, et sortir
de la preview est gratuit puisque les tampons du modèle n'ont jamais été
touchés. Le pipeline chunké existe déjà pour le modèle ; il faut l'utiliser en
**surimpression** et non en remplacement.

L'assemblage reste utile à un seul endroit : les tests d'égalité. Jamais sur le
chemin de frame.

### 4.5 L'identité du plan

Le cache de preview compare aujourd'hui l'**identité du pointeur partagé** du
plan. C'est fragile. L'analyse ne prouve pas qu'il rate à chaque frame — la
session renvoie normalement le même plan pour une requête identique — mais
quatre mécanismes réels provoquent des ratés : déplacement du plan de
construction, changement de sélection, de couleur active ou de profil (tous
vident la session alors qu'ils ne font pas partie de la clé de requête), et
simple aller-retour du pointeur entre deux cellules.

Correctif : comparer la **clé de requête par valeur** — elle possède déjà un
`operator==` et embarque identité, révision et génération du document. C'est
exactement l'hypothèse que fait déjà la session (« clé égale ⟹ plan égal »),
donc aucune hypothèse nouvelle. Pas de hachage, donc pas de collision possible.

Pour le renderer, on n'invente **pas** une empreinte de plan : une collision
signifierait un envoi manqué, donc une preview visuellement fausse. On expose
un ordinal monotone incrémenté à chaque recomposition réelle.

L'identifiant de plan actuel n'est pas touché ; aucun appelant existant ne
change.

### 4.6 Le cas du trait continu

Les changements d'un trait **s'accumulent** : la boîte globale grandirait
jusqu'à retrouver la taille d'un chunk. La composition doit donc dériver ses
clés des positions changées — mêmes règles que le cache de mesh, voisins d'axe
inclus uniquement quand l'occupation change — et non d'une boîte globale. Le
volume sondé reste proportionnel à la matière changée, pas à l'enveloppe du
trait.

Un garde-fou explicite reste nécessaire : au-delà d'un volume sale à calibrer,
on retombe sur la présentation agrégée — jamais sur un rebuild complet.

## 5. Découpage en lots

Chaque lot est vert (suite bloquante + smokes) avant le suivant. Aucun ne
change le comportement visible avant le lot 6.

**Lot 0 — mesurer avant de concevoir.** Aucun code de production. Mesurer le
chemin **régional** sur scènes denses et creuses (boîtes 5³/11³/21³) : toute
l'architecture en dépend, et VF-0264 §3 signalait déjà ce trou. Chiffrer
l'assemblage séparément. Instrumenter les compositions par frame et le premier
champ divergent de la clé de requête, pour trancher §4.5 sur une session
réelle.

**Lot 1 — socle partagé.** Extraire l'arithmétique de chunk en en-tête commun ;
le cache de mesh conserve son API par alias, aucun appelant ne change. Extraire
la validation des changements du document en fonction constante, sans mutation
ni journal : il n'existera plus qu'une seule implémentation des règles de
rejet, ce qui étend « Preview == Commit » jusqu'aux cas d'échec.

**Lot 2 — la vue et la surcharge du builder.** La vue « document +
changements », et une surcharge régionale du constructeur de mesh qui la prend
en source. Test d'or : sur une partition de l'espace, l'union des maillages
régionaux égale le maillage complet du document muté, **par ensemble de
faces**. Décliné sur ajout, suppression, recoloration, suppression du dernier
voxel, ajout hors bornes, changement sur frontière de chunk et de région.

**Lot 3 — le compositeur chunké.** Overrides par chunk, filtre de faces pour
les chunks intersectant la zone sale, plafond global de faces respecté comme
dans le cache. Le compositeur actuel est **conservé mot pour mot** comme oracle
de test et comme repli. Triple oracle : nouveau compositeur = ancien
compositeur = maillage du document réellement commité.

**Lot 4 — identité du cache et unification du trait.** Comparaison par valeur
de la clé, ordinal de composition, suppression des trois champs ad hoc qui
suivent aujourd'hui le trait à la main.

**Lot 5 — rendu chunké différentiel.** Surimpression sur les chunks du modèle,
refus explicite si le modèle GPU n'est pas à la bonne révision (sinon la
preview se superposerait à un modèle périmé). Le chemin de chargement doit
passer au chunké, sans quoi la preview resterait monolithique jusqu'à la
première édition.

**Lot 6 — lever le plafond, sous preuve.** Dans cet ordre : lots verts, égalité
stricte démontrée, banc rejoué avec **compte d'allocations** (le contrôle le
plus dur de « plus aucune copie »), session réelle instrumentée sur 512 k et
1 M. Alors seulement le plafond tombe — remplacé par une garde sur le **volume
sale**, qui est la grandeur qui pilote réellement le coût.

## 6. Comment on prouve les contrats

**Aucune mutation du document** : révision inchangée ; `ChangesSince` renvoie
un journal **vide** après composition ; nombre de voxels, bornes et palette
inchangés ; et le maillage du document recalculé après composition est
identique à celui d'avant.

**Aucun rebuild global** : le nombre de chunks reconstruits est strictement
inférieur au nombre total de chunks ; le volume sondé égale exactement `|B⁺|` —
c'est l'invariant qui **interdit** toute dérive vers la granularité chunk ; les
chunks non touchés sont identiques **par adresse** à ceux du modèle ; et ces
compteurs sont **invariants à la taille du document**.

**Égalité stricte** : par ensemble de faces, pas par ordre. C'est un changement
de test à assumer — un test compare aujourd'hui les sommets dans l'ordre. Le
précédent existe et est documenté : VF-0262 a déjà acté que le mesh assemblé
par chunks porte exactement les faces d'un build complet, l'ordre pouvant
différer.

## 7. Ce qui n'est pas établi

Ces points doivent être levés par la mesure, pas par l'hypothèse.

1. ~~**Le coût du chemin régional sur documents creux.**~~ **Levé le 05/08 —
   l'architecture est validée par la mesure.** Une région de 11³ coûte 0,318 ms
   sur 15 625 voxels et 0,391 ms sur un million : **plat**. Même constat en 5³
   (0,019 → 0,021 ms) et en 21³ (2,35 → 3,32 ms). Le coût suit le volume de la
   région, à environ **0,25 µs par cellule**. Sur documents creux, dix à cent
   fois moins (0,005 ms pour 11³ dans une boîte 128³ peu peuplée) :
   l'heuristique de sondage ne dégénère pas, y compris sur le chemin régional
   que VF-0264 §3 n'avait pas pu couvrir.

   Conséquence chiffrée : un pinceau 9³ donne une zone sale de 11³, donc
   **environ 0,32 ms** — quatre fois mieux que l'estimation du plan, contre
   1 354 ms aujourd'hui.
2. **La cause exacte des ratés de cache.** Quatre mécanismes nommés, aucun
   prouvé comme celui qui domine. Le correctif est robuste aux quatre, mais la
   mesure doit nommer le vrai.
3. **Le nombre réel d'overrides par mouvement**, et sur un trait long. Il pilote
   le coût du filtre et de l'envoi GPU.
4. **L'anomalie `sparse10k` en boîte 128³** (VF-0264 §3 : p50 9,1 ms, pire cas
   40,6 ms). Si elle vient du chemin régional, elle nous concerne directement.
5. **Tester le renderer sans GPU.** À vérifier avant de promettre les tests du
   lot 5 ; à défaut ils passeront par les smokes, plus lents et moins précis.

## LOT 4 — attribution du coût résiduel (05/08/2026)

### Ce qui a été réfuté

L'hypothèse « les 20-25 ms de `hl-handoff` sont l'envoi GPU » est **fausse**. Les
quatre appels que la sonde englobe — `ConfigureHighlights`,
la branche preview exacte, `ConfigureVoxelPreview`, `ConfigureTransformPreview` —
sont tous du bookkeeping CPU à court-circuit ; aucun n'émet de commande GPU.
L'envoi réel a lieu dans `EnsureHighlights` pendant le rendu, et `vp-render`
mesure 0,2 à 0,7 ms. Quatre sous-sondes (`ho-configure`, `ho-exact`, `ho-voxel`,
`ho-transform`) ont été posées pour attribuer ce coût par la mesure.

### Ce qui a été établi

`hl-plan` n'est pas la planification : le planner coûte 1,8 ms et **n'est pas
appelé** pendant un trait (`ShouldResolvePreviewForPresentation` retourne
`!strokeActive`). Le journal montre un coût **croissant au fil du trait** —
8,7 → 10,2 → 12,6 → 13,7 → 14,0 ms sur une même séquence. La cause est
structurelle : plusieurs postes suivent le **trait accumulé** au lieu du geste
courant.

1. `SmartToolStroke::Changes()` reconstruisait et **retriait** la liste complète
   du trait à chaque frame — O(S log S) pour un geste d'un voxel.
2. `ValidateVoxelChanges` construit un `unordered_set` de taille S par frame.
3. `VoxelChangeOverlay::overrides_` : `unordered_map` de taille S par frame.
4. `VoxelChangeOverlay::DirtyBounds()` est la boîte de **tous** les changements
   accumulés, dilatée de 1 : le nombre de chunks recomposés croît avec le trait.

### LOT 4b — livré

`ChangesView()` : vue `std::span` mémoïsée sur `revision_`, invalidée à chaque
mutation du trait. Supprime le poste 1 aux deux sites chauds
(`EditorWorkspaceHighlights.cpp:311` et `:365`). `Changes()` est conservé à
l'identique pour les 30 appelants existants et délègue à la vue.

**Correction de justesse trouvée au passage** : `Accumulate` pouvait fusionner
des cellules puis sortir en `return false` sur désaccord `Before`, **sans
incrémenter `revision_`**. Les caches indexés sur `Revision()` — dont la preview
exacte — pouvaient donc présenter un état périmé. Le bump manquant est ajouté.

### LOT 4c — conçu, non commencé, et volontairement séquencé après la mesure

**Théorème de réutilisation.** L'override d'un chunk vaut
`Mesh(document ⊕ tous les changements) ∩ chunk`. Le document n'est **pas** muté
pendant un trait : les changements restent en attente jusqu'au commit. Et la
visibilité d'une face ne consulte que les 6 voisins. Donc si aucun changement
nouveau ne tombe dans `chunk` dilaté de 1, son override est **inchangé** et peut
être réutilisé tel quel — y compris son tampon GPU.

**Piège écarté.** On ne peut PAS identifier les changements nouveaux par
comparaison de préfixe : `ChangesView()` est triée **par position**, pas par
ordre d'accumulation, donc une cellule ajoutée s'insère à un rang arbitraire.
Toute conception fondée sur « les N premiers sont déjà composés » est fausse.

**Conception retenue.** L'état incrémental garde, par clé de chunk, le nombre de
changements tombant dans ce chunk dilaté de 1, plus l'override déjà calculé.
À chaque composition on reparcourt les changements une fois pour recompter — du
travail entier, sans hachage, sans tri, sans maillage — et on ne recompose que
les chunks dont le compte a bougé. Le poste passe de « recomposer toute la boîte
sale cumulée » à « recomposer les chunks réellement touchés ».

Invalidation totale requise sur : identité du document, révision du document,
`ModelIndex`, et changement du jeu de chunks (modèle rechargé, cache vidé).

**Pourquoi ce lot attend la session de sondes.** `hl-plan` vaut 8-16 ms et
`hl-handoff` 20-25 ms. Les sous-sondes `ho-*` ne sont pas encore relevées : si
elles attribuent les 20 ms à `ho-configure`, le défaut dominant est dans la
comparaison des highlights et n'a rien à voir avec le compositeur. Construire un
compositeur à état avant de savoir cela optimiserait le poste le plus petit en
laissant le plus gros intact. La mesure d'abord.
