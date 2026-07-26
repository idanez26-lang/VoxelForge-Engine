# AR-0104 — Smart Tool Roadmap Validation

| Champ | Valeur |
|---|---|
| Projet | VoxelForge Studio |
| Audit | AR-01 |
| Révision | `e01b10047a818b2dd28d65c3934e1d6bf83fc7a6` |
| Point de départ | Brush Profiles + SMART-01 + SMART-02 + AR-01 terminés |
| Gate suivant | SMART-02.5 — Plan Contract Hardening |
| Prochaine fonction | SMART-03 — Actions, bloqué jusqu'à SMART-02.5 |

## 1. Décision de roadmap

La direction Controller -> Session -> Planner -> Plan doit être conservée.
SMART-01/02 ne doivent pas être annulés ni remplacés.

En revanche, SMART-03 ne doit pas commencer directement sur
`SmartToolPlanCell` actuel. Un lot correctif très court doit figer la
transition de cellule avant que Paint, Replace et Pick soient multipliés.

```text
SMART-01 — terminé
  |
  v
SMART-02 — terminé
  |
  v
AR-01 — terminé
  |
  v
SMART-02.5 — requis
  |
  v
SMART-03 — bloqué jusqu'à SMART-02.5
  |
  v
SMART-04 — Modes and Scene Context
  |
  v
SMART-05 — Shapes and Resolver Ports
  |
  v
SMART-06 — Face
  |
  v
SMART-07 — Line
```

Rectangle et Fill suivent après stabilisation de Face/Line. Ils ne doivent pas
être regroupés artificiellement dans SMART-07.

## 2. Gate préalable — SMART-02.5 Plan Contract Hardening

### Objectif

Corriger uniquement les contrats qui bloquent les actions, sans UX nouvelle.

### Périmètre minimal

1. cellule planifiée avec Before/After et flags ;
2. lecture de cellule en valeur dans la requête ;
3. invalidation automatique du plan lors d'un changement de Session ;
4. résultat/diagnostic générique non dépendant du code Brush ;
5. adapter générique plan -> `VoxelEditOperation` ;
6. tests de stale settings, plan autonome et conversion historique.

### Hors périmètre

- nouvelle action visible ;
- Face, Line, Rectangle, Fill ;
- UI ;
- optimisation ;
- Brush Profiles ;
- plugins ou réseau.

### Gate de sortie

- Add/Erase gardent exactement leur comportement ;
- Preview et Commit partagent toujours le même pointeur ;
- l'opération Undo peut être construite depuis le plan sans lecture métier ;
- aucun plan n'est commitable après mutation de settings sans nouveau Preview ;
- 149 tests existants et nouveaux tests verts.

## 3. SMART-03 — Actions

### Contenu recommandé

- actions canoniques Add, Remove, Paint, Replace, Pick ;
- migration de Erase -> Remove avec adaptateur de session/profil ;
- source Replace explicite ;
- résultat de contexte pour Pick ;
- `IActionResolver` interne ou table de stratégie ;
- migration Paint vers Planner/Plan ;
- suppression du resolver Legacy Paint et du chemin `VoxelPaintBrushTool`
  lorsqu'aucun caller ne subsiste ;
- un seul adapter Commit.

### Tests gate

- chaque action sur cellule vide/occupée/même couleur/couleur différente ;
- Preview == Plan == Commit ;
- Before/After exact ;
- no-op ;
- palette invalide ;
- stale revision/settings ;
- Undo/Redo atomique ;
- Pick sans historique ;
- aucun appel Brush Engine au Commit ;
- plus aucun chemin Legacy Paint.

### Décision

SMART-03 est **bloqué jusqu'à SMART-02.5**, puis validé.

## 4. SMART-04 — Modes and Scene Context

### Contenu recommandé

- `SmartMode` canonique : SingleVoxel, Brush, Face, Line, Rectangle, Fill ;
- séparation définitive Mode / Shape / Action ;
- snapshot Scene/Workplane versionné ;
- phases de geste et ancres ;
- politiques de limites/snap/overwrite ;
- invalidation document/génération/sous-modèle ;
- builder de requête hors `EditorWorkspace`.

### Tests gate

- requête sans UI/renderer ;
- cache key exhaustive ;
- changement de Workplane invalide le plan ;
- changement d'ancre invalide le plan ;
- même input = même clé et même résultat ;
- cancellation/document switch ;
- Session sans pointeur document.

### Décision

Validé sous réserve du contrat SMART-02.5.

## 5. SMART-05 — Shapes and Resolver Ports

### Contenu recommandé

- `IBrushResolver` interne ;
- SmartBrushEngine comme implémentation Cube/Sphere ;
- Cylinder déterministe ;
- consolidation des anciens services Shape lorsque pertinent ;
- registre interne de resolver versionné, sans API plugin publique prématurée ;
- ordre canonique des cellules.

### Tests gate

- tailles paires/impaires ;
- orientations ;
- overflow ;
- déterminisme ;
- absence de doublons ;
- résolution par version ;
- resolver inconnu refusé proprement ;
- plan sérialisable conceptuellement sans callback.

### Décision

Validé. Ne pas ouvrir l'API plugin avant que deux resolvers concrets aient
prouvé le contrat.

## 6. SMART-06 — Face

### Contenu recommandé

- identification de face dans le contexte Scene ;
- masque coplanaire connecté ;
- trous et contours irréguliers ;
- épaisseur ;
- Add/Remove/Paint/Replace via le même ActionResolver ;
- Face Add comme capacité d'extrusion V1, sans UX Extrude dédiée.

### Tests gate

- six normales ;
- formes irrégulières et trous ;
- limites ;
- gros masque ;
- Preview exact ;
- cache ;
- Undo/Redo ;
- aucun calcul renderer ;
- aucune logique Face dans `EditorWorkspace`.

### Décision

Validé après SMART-04/05. Le resolver Face ne doit pas introduire son propre
plan.

## 7. SMART-07 — Line

### Contenu recommandé

- Axis Line et Free 3D ;
- supercover déterministe ;
- Brush le long du trajet ;
- déduplication et ordre canonique ;
- interaction deux points dans la Session ;
- toutes les actions mutantes via le même plan.

### Tests gate

- axes, diagonales et inversion A/B ;
- tie-break stable ;
- taille Brush ;
- overlap ;
- limites ;
- Preview/Commit ;
- Undo/Redo ;
- cancellation ;
- pas de second service de rasterisation conservé.

### Décision

Validé après SMART-04/05. Rectangle et Fill doivent garder des lots séparés.

## 8. Capacité V1 à long terme

### Modes, Shapes, Face, Line, Rectangle, Fill

La structure peut les supporter sans remplacer Controller/Session/Planner/Plan,
à condition d'introduire maintenant :

- une opération de cellule complète ;
- des settings canoniques ;
- des resolvers internes ;
- un contexte Scene versionné.

### Extrude

Face Add + épaisseur est compatible. Une UX push/pull demandera de nouvelles
phases de Session, mais pas un nouveau plan.

### IA

L'IA peut préparer une requête ou proposer des settings. Pour être auditable et
rejouable, requête et plan devront remplacer callback/pointeur par identités,
seeds et versions stables.

### Macros

Les macros peuvent enregistrer commandes/settings/inputs ou plans. Elles
nécessitent un schéma versionné, des IDs stables et un ordre canonique.

### Plugins

Les plugins sont possibles si Planner appelle des interfaces internes avec
capability ID + version et si leurs sorties sont revalidées avant création du
plan. Ne pas exposer `SmartBrushEngine` ni des pointeurs document aux plugins.

### Collaboration future

Le plan Before/After est une bonne unité de proposition, mais la collaboration
exige :

- UUID document/sub-modèle ;
- base revision stable ;
- auteur/site/operation ID ;
- ordre canonique ;
- stratégie de conflit ;
- sérialisation endian-safe ;
- aucune identité `uintptr_t` ;
- aucun `PlanId` process-local comme identité réseau.

Le modèle actuel ne satisfait pas encore ces exigences, mais le durcissement
proposé évite une future refonte du cœur.

## 9. Critères de non-régression permanents

À conserver pour chaque lot :

- Planner seul créateur de plan ;
- Preview et Commit consomment la même instance ;
- aucun recalcul géométrique au clic ;
- aucune lecture Brush Profile après planification ;
- aucune dépendance ImGui/SDL/D3D12/renderer dans le domaine ;
- une transaction par geste ;
- un rebuild par transaction ;
- plan invalidé par document/génération/révision/settings ;
- ordre déterministe ;
- test de parité avec le comportement migré ;
- suppression du chemin Legacy correspondant dans le même lot.

## 10. Recommandation finale

Ne pas commencer les actions fonctionnelles tant que le contrat de cellule et
la lecture document ne sont pas complets. Cette correction reste petite et
préserve tout le travail SMART-01/02.

**Architecture nécessitant une correction avant SMART-03.**
