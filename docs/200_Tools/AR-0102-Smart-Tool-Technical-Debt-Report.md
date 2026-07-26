# AR-0102 — Smart Tool Technical Debt Report

| Champ | Valeur |
|---|---|
| Projet | VoxelForge Studio |
| Audit | AR-01 |
| Révision | `e01b10047a818b2dd28d65c3934e1d6bf83fc7a6` |
| Portée | Dette observée après SMART-02 |
| Règle | Aucun changement fonctionnel dans cet audit |

## 1. Synthèse

La dette n'affecte pas la stabilité actuelle de Pencil Add/Erase. Elle affecte
la capacité à ajouter les actions et modes suivants sans créer de nouveaux
chemins parallèles.

| Niveau | Nombre | Décision |
|---|---:|---|
| Critique | 2 | corriger avant SMART-03 |
| Importante | 7 | planifier avant Face/Line/Fill |
| Mineure | 6 | traiter lors des migrations proches |
| Acceptable | 5 | conserver temporairement avec date de retrait |

### Matrice de traçabilité

La gravité est celle de la section détaillée correspondante. Cette matrice
rend explicites la preuve, l'impact, le lot recommandé et le critère de
résolution de chaque dette.

| ID | Gravité | Preuve dans le code inspecté | Impact | Lot recommandé | Critère de résolution |
|---|---|---|---|---|---|
| C-01 | Critique | `SmartToolPlanCell` ne stocke ni palette précédente ni existence avant/après | Paint/Replace et Undo ne peuvent pas être dérivés du plan seul | SMART-02.5 | Chaque cellule exprime une transition Before/After suffisante pour Preview, Commit et Undo |
| C-02 | Critique | `SmartBrushRequest::IsOccupied` ne retourne qu'un booléen et capture le document | Relire le document hors Planner reste nécessaire ; replay non portable | SMART-02.5 | Le Planner reçoit une vue read-only versionnée donnant existence et palette |
| I-01 | Importante | `LegacySmartBrushPreviewResolver`, `LegacySmartBrushPreviewCache` et `VoxelPaintBrushTool` restent actifs | Deux chemins métier peuvent diverger | SMART-03 | Paint utilise Controller/Planner/Plan ; le chemin Legacy Paint est supprimé |
| I-02 | Importante | Request, Result, Plan et Planner exposent les types de `SmartBrushEngine` | Le Planner deviendrait un switch monolithique avec les futurs modes | SMART-05, avant Face/Line | Des ports internes de resolver sont injectés sans changer l'API publique `Plan(request)` |
| I-03 | Importante | Les enums code et VF-0300 ne décrivent pas le même espace Action/Geometry/Mode | Combinaisons ambiguës et profils difficiles à migrer | SMART-03 puis SMART-04 | Une action et un mode canoniques existent avec migration explicite des profils |
| I-04 | Importante | Les setters de `SmartToolSession` ne vident pas le plan courant | Un Commit peut consommer un plan antérieur si aucun Preview n'a suivi | SMART-02.5 | Tout changement influent invalide le plan et un test couvre settings-change-before-commit |
| I-05 | Importante | `EditorWorkspace` choisit hit, workplane, position, normale, palette, action et adapters | Croissance continue du Workspace et duplication par mode | SMART-04 à SMART-07 | Le Workspace ne fait plus que router l'entrée vers des builders/adapters applicatifs |
| I-06 | Importante | Les interfaces Preview/Commit consumer existent sans implémentation de production | Frontière morte et contrat trompeur | SMART-03 | Les adapters réels utilisent ces ports, ou les interfaces sont retirées |
| I-07 | Importante | Le Plan conserve `SmartBrushResult` et les cellules finales | Positions et capacités de vecteurs sont dupliquées | SMART-02.5, puis benchmark Release | Le Plan ne garde que les opérations finales et les résumés nécessaires |
| M-01 | Mineure | `PlanId` et `Revision` reçoivent la même séquence locale | Sémantique ambiguë pour cache et diagnostics | SMART-02.5 | `PlanSequence` et `SourceRevision` sont distincts et testés |
| M-02 | Mineure | L'identité source est un `uintptr_t` | Impossible à persister ou échanger | Avant macros/réseau | Une identité stable est utilisée hors du cache local |
| M-03 | Mineure | L'UUID du profil participe à la clé même à réglages identiques | Cache miss sans changement métier | Après SMART-03 | La clé utilise une révision ou un hash des réglages résolus |
| M-04 | Mineure | `Workplane` est stocké mais ignoré par le Planner | État trompeur et futurs plans insuffisamment invalidés | SMART-04 | Le contexte de construction est consommé et inclus dans la clé, ou retiré |
| M-05 | Mineure | `activePaletteColor` est reçu puis ignoré par le resolver Add/Erase | API ambiguë et risque de divergence visuelle future | SMART-03 | Le paramètre est utilisé conformément au contrat ou supprimé |
| M-06 | Mineure | `SmartToolResult` expose `SmartBrushResultCode` | Diagnostics non génériques pour Face/Line/plugins | SMART-05 | Diagnostics Smart Tool indépendants du resolver Brush |
| A-01 | Acceptable | `VoxelEraserTool` reste appelé par compatibilité et tests | Surface historique maintenue temporairement | SMART-03 | Parité Remove atteinte et références production/tests migrées |
| A-02 | Acceptable | Box/Line/Sphere/Fill historiques restent séparés | Code dupliqué jusqu'à leur lot de migration | SMART-05 à SMART-07 et lots suivants | Chaque outil disparaît lorsque son resolver Smart Tool atteint la parité |
| A-03 | Acceptable | `VoxelPencilPreview` reste un type de présentation | Deux modèles de Preview subsistent nominalement | Après SMART-03 | Un type de Preview générique couvre inspector, renderer et tests |
| A-04 | Acceptable | `VoxelPencilTool` autorise un Commit sans History | Production pourrait contourner l'Undo si mal appelée | Avant release V1 | Le chemin production exige l'historique ; l'exception reste limitée aux tests |
| A-05 | Acceptable | `SmartBrushEngine` est directement le resolver Pencil | Couplage tolérable pour le seul resolver migré | SMART-05 | Il implémente le port Brush officiel sans duplication |

## 2. Dette critique

### C-01 — Le plan ne contient pas la transition Before/After

**Constat**

`SmartToolPlanCell` ne contient que position, Add/Erase/Ignore, palette de
destination et état de Preview. La palette précédente et les bits
d'existence avant/après sont absents.

**Conséquence**

- Paint et Replace ne peuvent pas être exprimés ;
- Erase relit le document au Commit ;
- Undo n'est pas constructible depuis le plan seul ;
- macros, IA et réseau ne peuvent pas inspecter une opération complète ;
- le Preview générique ne connaît pas exactement la transition.

**Risque**

Chaque nouvelle action pourrait introduire son propre evaluator et son propre
adaptateur historique, reproduisant le chemin Legacy Paint.

**Coût de correction**

Petit à moyen si traité avant SMART-03 ; fort après plusieurs actions.

**Correction minimale — SMART-02.5**

Introduire une opération de cellule à largeur fixe :

```cpp
struct VoxelOperation
{
    VoxelPosition Position;
    OperationType Operation;
    std::uint8_t PreviousPalette;
    std::uint8_t NewPalette;
    std::uint32_t Flags;
};
```

Les flags doivent au minimum distinguer `ExistedBefore`, `ExistsAfter`,
`NoOp`, `Clipped` et `Invalid`. Le type ne doit pas être sérialisé par copie
brute de sa mémoire.

### C-02 — La requête ne sait lire que l'occupation

**Constat**

`SmartBrushRequest::IsOccupied` retourne un booléen et capture le document par
lambda.

**Conséquence**

- le Planner ne connaît pas la palette précédente ;
- Paint/Replace doivent relire le document ailleurs ;
- requête non sérialisable ;
- dépendance implicite à un document mutable ;
- replay, macro, IA asynchrone, plugin isolé et collaboration impossibles avec
  ce contrat.

**Coût de correction**

Petit avant SMART-03.

**Correction minimale — SMART-02.5**

Remplacer l'interrogation booléenne par un accès en lecture explicite, par
exemple `optional<uint8_t> ReadPalette(VoxelPosition)`, adossé à une
révision/génération figée. À moyen terme, préférer une vue Scene/document
read-only versionnée.

## 3. Dette importante

### I-01 — Paint reste un second chemin métier

**Utilisateurs**

- `LegacySmartBrushPreviewResolver` ;
- `LegacySmartBrushPreviewCache` ;
- `VoxelPaintBrushTool::Evaluate` ;
- `VoxelPaintBrushTool::Apply` ;
- branches Paint dans `EditorWorkspace`.

**Pourquoi**

SMART-02 a migré Pencil Add/Erase sans étendre le plan aux transitions de
couleur.

**Retrait**

SMART-03 Actions, immédiatement après SMART-02.5.

### I-02 — Le domaine dépend du moteur Brush concret

`SmartToolRequest`, `SmartToolResult`, `SmartToolPlan` et `SmartToolPlanner`
exposent directement les types du `SmartBrushEngine`.

**Risque**

Face, Line, Rectangle, Fill et plugins pousseront le Planner vers un switch
monolithique.

**Retrait**

Introduire des interfaces internes `IModeResolver`, `IBrushResolver` et
`IActionResolver` avant le premier mode non Brush. L'API publique peut rester
`Plan(request)`.

### I-03 — Enums canoniques non alignés avec VF-0300

- `SmartAction` contient Erase, Fill, Smooth, Noise, Material, Random et AI ;
- VF-0300 retient Add, Remove, Paint, Replace, Pick ;
- `SmartGeometry` mélange Pencil/Cube/Sphere avec Face/Box/Line ;
- `SmartBrushMode` duplique encore l'action.

**Risque**

Combinaisons ambiguës, profils difficiles à migrer et matrice action × mode
incohérente.

**Retrait**

SMART-03 doit fixer l'action canonique ; le lot Mode doit ensuite séparer
`SmartMode` et `SmartBrushShape`.

### I-04 — Session non auto-invalidante

Les setters de `SmartToolSession` modifient Geometry, Action, Brush, profil ou
Workplane sans vider le plan courant. Le Controller replanifie normalement au
frame suivant grâce à la clé, mais `ResolveCommit` peut rendre l'ancien plan si
un caller modifie la Session puis commit avant un nouveau Preview.

**Risque**

Contrat fragile dépendant de l'ordre d'appel du Workspace.

**Correction**

Chaque setter influençant le plan doit appeler `Clear()` uniquement lorsque sa
valeur change. Ajouter un test « settings change -> commit refused until
preview ».

### I-05 — EditorWorkspace concentre la traduction métier

Le Workspace choisit :

- hit ou Workplane ;
- voxel hit ou adjacent ;
- normale ;
- palette ;
- action admise ;
- resolver Preview ;
- outil Commit.

**Risque**

Chaque mode futur augmente une classe déjà très large et couplée.

**Retrait**

Déplacer progressivement la construction de requête et le cycle du geste dans
`SmartToolController`/builders applicatifs. Ne pas déplacer ImGui dans le
domaine.

### I-06 — Contrats consommateurs non utilisés

`SmartToolPreviewPlanConsumer` et `SmartToolCommitPlanConsumer` existent mais
aucune implémentation ne les consomme. `EditorWorkspace` appelle directement
resolver et outil.

**Risque**

Abstraction morte et fausse impression que Controller pilote l'exécution.

**Décision**

Soit brancher de vrais adapters génériques, soit supprimer ces interfaces
jusqu'à leur besoin. Ne pas conserver deux frontières.

### I-07 — Plan trop redondant

Le plan conserve `SmartBrushResult` complet plus les cellules finales. Les
positions sont dupliquées et quatre vecteurs du Brush réservent chacun le
volume complet.

**Risque**

Mémoire et allocations importantes pour Face, Fill, Line épaisse et drag.

**Retrait**

Le BrushResult doit devenir un intermédiaire du Planner. Le plan final conserve
opérations, bounds, statistiques, render summary et diagnostics, pas les listes
de classification transitoires.

## 4. Dette mineure

### M-01 — `PlanId` et `Revision` sont identiques

Renommer en `PlanSequence` et exposer séparément `SourceRevision`.

### M-02 — Identité document process-local

`uintptr_t` est une bonne garde locale, mais ne doit pas entrer dans un format
portable. Ajouter plus tard un `DocumentUuid` stable pour macros/réseau.

### M-03 — UUID du profil dans la clé géométrique

Deux profils différents mais résolus en réglages identiques produisent un
cache miss. Conserver l'UUID comme provenance, utiliser une révision/hash des
réglages pour le cache.

### M-04 — Workplane ambigu

Le champ `Workplane` est stocké par la Session, ignoré par le Planner et absent
de la clé. Remplacer par un vrai contexte de construction ou le retirer.

### M-05 — Couleur active inutilisée par le Preview Add/Erase

`SmartBrushPreviewResolver::Resolve` reçoit `activePaletteColor` mais
l'ignore. Clarifier si le Ghost doit montrer la palette réelle ou un code
d'action.

### M-06 — Gestion d'erreur basée sur le code Brush

`SmartToolResult` expose encore `SmartBrushResultCode`. Introduire des
diagnostics génériques avant l'arrivée de resolvers non Brush.

## 5. Dette acceptable de migration

### A-01 — VoxelEraserTool historique

Conservé pour les raccourcis et tests historiques. Retrait après parité complète
Remove dans SMART-03.

### A-02 — Box/Line/Sphere/Fill historiques

Leurs lots Smart Tool ne sont pas commencés. Ils servent de référence et
doivent disparaître lors de chaque migration fonctionnelle, pas avant.

### A-03 — VoxelPencilPreview comme type de présentation

Le calcul Pencil historique a été retiré de la production. Le type reste
utilisé par inspector/layout/tests et peut être consolidé avec le preview
générique plus tard.

### A-04 — Chemin Commit sans History

Acceptable uniquement pour tests/compatibilité immédiate. Le chemin production
doit exiger l'historique avant la release V1.

### A-05 — SmartBrushEngine comme resolver Pencil

Acceptable tant qu'il devient l'implémentation du futur `IBrushResolver` et
n'est pas dupliqué.

## 6. Dette Brush Profiles

Le service est fonctionnel et isolé du document, mais son schéma reste lié aux
enums prototypes et au `SmartTool`.

À planifier :

- settings canonique sérialisable ;
- scopes factory/user/project ;
- migration de l'enum Erase vers Remove ;
- options de Mode ;
- source Replace ;
- version des algorithmes ;
- provenance palette portable ;
- séparation catalogue/CRUD et sélection compacte.

Il ne faut pas modifier Brush Profiles pendant la correction AR-01. Les
nouveaux settings devront disposer d'un adaptateur de migration explicite.

## 7. Priorités

| Priorité | Action | Gate |
|---|---|---|
| P0 | C-01 + C-02 + I-04 | SMART-02.5, avant SMART-03 |
| P0 | adapter générique Plan -> History | dans SMART-03 |
| P0 | migrer Paint et supprimer le resolver Legacy | fin SMART-03 |
| P1 | actions/modes/shapes canoniques | SMART-03/SMART-04 |
| P1 | ports internes de resolver | avant Face/Line |
| P1 | réduire EditorWorkspace | au fil de SMART-04 à SMART-07 |
| P2 | compacter le plan et le preview | après mesures de release |
| P2 | identité portable et version resolver | avant macros/plugins/réseau |

## 8. Critère de fermeture AR-01

AR-01 ne demande aucune correction de code immédiate. La dette critique est
documentée et circonscrite. SMART-02.5 doit être validé avant le premier
développement fonctionnel SMART-03.
