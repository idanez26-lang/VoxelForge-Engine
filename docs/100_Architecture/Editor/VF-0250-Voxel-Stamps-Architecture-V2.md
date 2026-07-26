# VF-0250 — Voxel Stamps Architecture V2

| Champ | Valeur |
|---|---|
| Projet | VoxelForge Studio |
| Statut | Architecture officielle approuvée |
| Version du document | 2.0 |
| Date | 2026-07-24 |
| Principe produit | Créer plus vite. Rester l'artisan. |

## Architecture Status

| Champ | Valeur |
|---|---|
| Status | Approved |
| Version | V2 |
| Implementation | Not Started |

## 1. Vision générale

Un Voxel Stamp est une création voxel issue du travail de l'utilisateur, enregistrée comme contenu réutilisable puis replacée autant de fois que nécessaire.

Le terme **Stamp** reste le nom technique du domaine, du format et des métadonnées. L'interface utilisateur emploie une formulation plus directe :

```text
Créer
  ↓
Selection
  ↓
Save Selection As...
  ↓
Nom
  ↓
Forge Library
  ↓
Placement
```

Cette terminologie évite d'imposer un concept technique supplémentaire. L'utilisateur comprend immédiatement qu'il sauvegarde ce qu'il vient de sélectionner. Il découvre ensuite naturellement que cette création devient réutilisable depuis la Forge Library.

Le système ne doit pas être assimilé à un simple copier-coller. Il apporte :

- un UUID stable ;
- des métadonnées durables ;
- un pivot reproductible ;
- une palette portable ;
- une miniature ;
- une organisation dans la Forge Library ;
- un placement continu ;
- un Live Voxel Preview exact ;
- des variantes ;
- une compatibilité future avec les Prefabs, l'IA et les plugins.

## 2. Intégration dans l'interface officielle

L'organisation actuelle de VoxelForge Studio est conservée :

```text
┌────────────────┬──────────────────────────────┬──────────────────┐
│ CREATE         │ VIEWPORT                     │ REUSE            │
│                │                              │ Forge Library    │
│ Smart Tool     │ Live Voxel Preview           │ Assets          │
│ Selection      │ Placement                    │ Categories      │
│ Transform      │                              │ Search          │
├────────────────┴──────────────────────────────┴──────────────────┤
│ BUILD                                                         │
└─────────────────────────────────────────────────────────────────┘
```

Règles permanentes :

- CREATE reste à gauche ;
- le viewport reste central et prioritaire ;
- REUSE et la Forge Library restent à droite ;
- BUILD reste en bas ;
- la Toolbar reste limitée à Smart Tool, Selection et Transform ;
- aucun panneau permanent supplémentaire n'est requis ;
- les options avancées sont repliées ;
- les diagnostics techniques ne reviennent pas dans le mode de création.

Le Smart Tool reste uniquement consacré à la création. Les bibliothèques ne doivent pas être intégrées à ses options.

## 3. Responsabilités de Selection

Selection devient la porte d'entrée officielle des créations réutilisables.

```text
Selection
  │ capture les voxels et les bounds
  ▼
Stamp Capture
  │ crée un asset voxel autonome
  ▼
Forge Library
  │ organise, recherche et présente
  ▼
Placement
  │ insère dans le document
  ▼
Prefab futur
    ajoute structure, comportements et liens
```

### 3.1 Répartition des responsabilités

| Système | Responsabilité |
|---|---|
| Selection | Déterminer les voxels et les limites spatiales à capturer |
| Stamp Core | Normaliser et sérialiser la création |
| Forge Library | Classer, rechercher, mettre en favori et présenter |
| Placement | Transformer, prévisualiser, valider puis insérer |
| Prefab futur | Ajouter structure, hiérarchie, composants, comportements et liens |

Un Stamp contient un fragment voxel fixe. Un Prefab pourra contenir plusieurs éléments, des relations, des points d'attache, des comportements et des paramètres. Enregistrer une sélection ne doit donc jamais créer implicitement un Prefab.

## 4. Architecture technique

### 4.1 Vue d'ensemble

```text
SelectionService
      │
      ▼
StampCaptureService
      │
      ▼
VoxelStamp
      ├──────────────► VfstampSerializer
      ├──────────────► StampValidationService
      ├──────────────► StampThumbnailService
      └──────────────► StampLibraryService
                              │
                              ▼
                         Forge Library
                              │
                              ▼
                  StampPlacementController
                              │
                 ┌────────────┴────────────┐
                 ▼                         ▼
          Constraint Engine       Smart Placement Hints
                 └────────────┬────────────┘
                              ▼
                   StampPlacementPlanner
                              │
                              ▼
                    Common Preview Contract
                              │
                              ▼
                    Live Voxel Preview
                              │
                              ▼
                     Validation finale
                              │
                              ▼
                  Opération atomique Undo/Redo
```

### 4.2 Domaine indépendant

Les composants suivants ne dépendent ni d'ImGui, ni du renderer, ni du docking, ni du viewport :

- `VoxelStamp`
- `StampCaptureService`
- `StampValidationService`
- `StampPlacementPlanner`
- `StampPaletteMapper`
- `StampLibraryService`
- `StampSearchIndex`
- `VfstampSerializer`
- `StampVariantGroup`

### 4.3 Couche application

`StampWorkflowController` coordonne le document actif, Selection, la bibliothèque, la session de placement, la preview et l'historique. Il ne calcule ni les voxels ni le rendu.

`StampPlacementSession` contient uniquement l'état temporaire du placement :

- asset ou groupe actif ;
- variante résolue ;
- pivot utilisé ;
- position ;
- rotation ;
- miroir ;
- contraintes ;
- numéro du prochain placement ;
- état de validation.

Cet état n'est jamais écrit dans le fichier Stamp.

### 4.4 Couche UI

La couche UI utilise :

- `StampLibraryViewModel`
- `StampCardViewModel`
- `SaveSelectionViewModel`
- `StampPlacementStatusViewModel`

L'UI ne lit ni n'écrit directement les voxels.

## 5. Modèle de données

### 5.1 Données portables

```text
VoxelStamp
├── FormatVersion
├── UUID
├── Name
├── Author
├── CreatedAt
├── ModifiedAt
├── ContentHash
├── Bounds
│   ├── Minimum
│   ├── Maximum
│   └── Dimensions
├── Pivot
│   ├── RequestedMode
│   ├── ResolvedMode
│   ├── LocalPosition
│   ├── LocalNormal
│   └── AutoPolicyVersion
├── Palette locale
├── Voxels
├── Tags
├── Category
├── Description
├── SmartPlacementMetadata
├── VariantMetadata optionnelle
├── Thumbnail optionnelle
└── Extensions futures
```

Chaque voxel contient une position locale entière et un identifiant de couleur local. Les positions sont normalisées relativement au minimum de la sélection.

### 5.2 Palette

Le Stamp stocke les couleurs RGBA réellement utilisées :

```text
StampPaletteEntry
├── LocalColorId
├── RGBA
└── SourcePaletteIndex optionnel
```

Au placement :

1. une couleur RGBA identique est réutilisée ;
2. sinon une entrée libre de la palette cible est réservée ;
3. si aucune entrée n'est disponible, le placement est invalidé ;
4. aucune couleur proche n'est choisie silencieusement.

Les changements de palette et les changements de voxels doivent appartenir à la même transaction.

### 5.3 État de bibliothèque séparé

Favoris, récents et collections ne font pas partie du contenu portable :

```text
StampLibraryRecord
├── StampUUID
├── SourcePath
├── Favorite
├── LastUsedAt
├── UsageCount
├── Collections
├── CachedThumbnail
└── SearchIndexRevision
```

Cette séparation évite de modifier le Stamp lorsqu'un utilisateur change son organisation personnelle.

## 6. Workflow Save Selection As...

### 6.1 Déroulement

1. L'utilisateur active Selection.
2. Il sélectionne les voxels à réutiliser.
3. La commande `Save Selection As...` devient disponible.
4. Le système capture un snapshot de la sélection, du document et de sa révision.
5. Les coordonnées sont normalisées.
6. Les couleurs réellement utilisées sont compactées.
7. Auto Pivot propose un pivot et montre son emplacement.
8. L'utilisateur saisit un nom et peut préciser catégorie, tags ou pivot.
9. Il choisit Project Library ou My Library.
10. Le fichier `.vfstamp` est validé puis installé atomiquement.
11. Le catalogue et la miniature sont mis à jour.
12. La nouvelle création est sélectionnée dans la Forge Library.

### 6.2 Règles

- la commande est désactivée pour une sélection vide ;
- les doublons de noms sont autorisés grâce aux UUID ;
- un remplacement est toujours explicite ;
- une modification du document pendant le dialogue déclenche une nouvelle validation ;
- la sélection source n'est ni déplacée ni supprimée ;
- l'échec d'écriture ne doit laisser ni fichier partiel ni catalogue incohérent.

## 7. Auto Pivot

### 7.1 Objectif

Auto est le mode par défaut. Il choisit une stratégie parmi :

- Bottom Center ;
- Center ;
- Surface ;
- Corner.

L'utilisateur peut toujours forcer Center, Bottom, Surface ou Corner.

### 7.2 Contexte analysé

Le résolveur Auto reçoit un contexte explicite et stable :

- override utilisateur éventuel ;
- type de placement : surface, Workplane, grille précise ou libre ;
- point et normale de la surface ;
- orientation du Workplane ;
- bounds et dimensions de la sélection ;
- distribution des voxels ;
- contacts entre voxels sélectionnés et non sélectionnés.

Ces entrées sont normalisées avant résolution. Le résolveur n'utilise ni position du pointeur non quantifiée, ni approximation écran, ni état d'une frame précédente.

### 7.3 Règles officielles

```text
Override utilisateur ?
  └── Oui : preset forcé

Alignement précis sur grille ?
  └── Oui : Corner

Surface horizontale supérieure ?
  └── Oui : Bottom Center

Surface verticale ?
  └── Oui : Surface

Placement libre ou ambigu ?
  └── Center
```

#### Objet posé au sol

Une surface horizontale supérieure ou un Workplane horizontal utilise Bottom Center. Le pivot est placé au centre de la base du contenu et sa normale locale est orientée vers le haut.

#### Objet mural

Une surface verticale utilise Surface. Le pivot est placé sur la face d'attache enregistrée et sa normale pointe vers l'extérieur du support.

#### Objet centré

Un placement libre, sans surface exploitable ou ambigu utilise Center.

#### Corner

Corner est utilisé lorsqu'un alignement précis sur la grille est demandé. Le coin est choisi dans le repère local selon les signes de la direction de placement, avec un ordre de départage fixe X, puis Y, puis Z.

#### Cas ambigu

Center est le fallback officiel. L'interface affiche le pivot résolu et permet toujours de forcer Center, Bottom, Surface ou Corner.

### 7.4 Stabilité

Auto Pivot est déterministe : mêmes voxels, même contexte normalisé, même version de politique et même override produisent exactement le même résultat.

Il est calculé une seule fois lors de la capture. Le résultat résolu est enregistré dans le Stamp avec la version de la politique et le contexte discret ayant conduit au choix.

Il n'est jamais recalculé silencieusement au chargement. Une future commande `Recalculate Auto Pivot` sera explicite.

Le pivot est stocké en coordonnées fixes, par exemple en unités de `1/256 voxel`, afin de représenter exactement les centres de dimensions paires, les faces et les futurs pivots manuels.

## 8. Forge Library orientée contenu

L'artiste cherche naturellement une fenêtre, un arbre ou une chaise. Il ne cherche pas d'abord un format interne.

La navigation principale est donc orientée contenu :

```text
Forge Library
├── Creations
│   ├── Architecture
│   │   ├── Windows
│   │   ├── Doors
│   │   └── Walls
│   ├── Nature
│   │   ├── Trees
│   │   ├── Rocks
│   │   └── Plants
│   ├── Furniture
│   ├── Sci-Fi
│   ├── Dungeon
│   └── Custom Categories
├── Brushes
├── Favorites
└── Recent
```

Le type technique reste disponible comme filtre secondaire :

- Brush ;
- Stamp ;
- Prefab ;
- Material ;
- Palette ;
- Texture ;
- Scene.

Les catégories et sous-dossiers sont personnalisables. Ils ne doivent pas être représentés par un enum compilé.

### 8.1 Carte de contenu

Une carte présente :

- miniature ;
- nom ;
- catégorie ;
- dimensions ;
- nombre de voxels ;
- favori ;
- groupe de variantes éventuel ;
- source Project/My Library ;
- avertissement de compatibilité.

### 8.2 Project Library et My Library

Deux espaces distincts sont officiels :

#### Project Library

- appartient au projet ;
- est stockée sous le Project Root ;
- est partageable avec le projet ;
- utilise uniquement des chemins relatifs au projet, des UUID et des content hashes ;
- possède son propre catalogue versionné ;
- peut être reconstruite depuis les assets du projet.

Disposition recommandée :

```text
<ProjectRoot>/
└── Assets/
    └── ForgeLibrary/
        ├── Creations/
        ├── Brushes/
        └── ForgeCatalog.json
```

#### My Library

- appartient au profil utilisateur ;
- est disponible dans tous les projets ;
- possède son propre catalogue ;
- n'est pas implicitement copiée ou partagée avec un projet.

Disposition recommandée sous Windows :

```text
%LOCALAPPDATA%/
└── VoxelForge Studio/
    └── Library/
        ├── Creations/
        ├── Brushes/
        └── ForgeCatalog.json
```

Un projet partagé ne stocke jamais de chemin absolu vers My Library. Une référence portable utilise UUID, content hash et scope explicite. Si un futur Prefab ou une Scene doit dépendre durablement d'un asset personnel, celui-ci doit être importé explicitement dans Project Library ou incorporé selon une politique validée. Un simple placement de Stamp reste autonome, puisque les voxels résultants sont enregistrés dans le document.

## 9. Brush Library

Les Brush Profiles quittent totalement le Smart Tool.

```text
Forge Library
├── Creations
│   └── catégories orientées contenu
├── Brushes
│   ├── Collections
│   └── réglages d'outil
├── Favorites
└── Recent
```

Différence permanente :

| Brush | Stamp |
|---|---|
| Enregistre des réglages d'outil | Enregistre des voxels réels |
| Forme, taille, action, orientation | Géométrie, couleurs, dimensions, pivot |
| Modifie le comportement du Smart Tool | Active un mode Placement |
| Ne contient pas de création utilisateur figée | Représente une création réutilisable |

Le Smart Tool reste un outil de création et ne devient pas un navigateur de bibliothèque.

## 10. Live Voxel Preview

Principe permanent :

> Ce que l'utilisateur voit avant validation correspond au résultat créé.

Le preview principal ne doit pas être remplacé par une simple boîte filaire.

### 10.1 Contrat commun futur

```text
PreviewRequest
├── SourceKind
├── Voxels
├── Palette
├── Transform
├── Pivot
├── PlacementPolicy
└── DocumentRevision
        │
        ▼
PreviewPlan
├── ValidVoxels
├── CollisionVoxels
├── OutOfBoundsVoxels
├── PaletteConflicts
├── Bounds
├── PivotRenderData
└── CanCommit
```

Le renderer reçoit uniquement `PreviewRenderData`. Il ne calcule ni positions, ni couleurs, ni collisions.

### 10.2 Unified placement plan

`StampPlacementPlan` est la source de vérité immuable d'une tentative de
placement. Il est construit une seule fois par `StampPlacementPlanner`, puis
consommé sans recalcul métier par l'adaptateur de preview et par l'adaptateur
de transaction :

```text
StampPlacementSession
        |
        v
StampPlacementPlanner
        |
        v
StampPlacementPlan (immutable)
        |                         |
        v                         v
StampLivePreviewBuilder   PlaceVoxelStampOperation
        |                         |
        v                         v
VoxelPreviewData          VoxelEditTransaction
```

Le plan contient au minimum :

- l'identité du Stamp ;
- l'identité process-local du document, sa génération et sa révision ;
- le sous-modèle cible ;
- le pivot, la transformation et la politique de collision ;
- le plan de palette complet ;
- les bounds monde ;
- les statistiques et diagnostics ;
- `CanCommit` ;
- pour chaque voxel : ordinal source, position locale, position monde,
  index palette local et document, valeur précédente, valeur finale,
  chevauchement et hors-limites ;
- une clé de cache couvrant tous les paramètres qui influencent le résultat.

Le preview et le clic de validation ne relisent pas le document pour refaire
ces décisions. Au clic, l'identité, la génération, la révision et le
sous-modèle du plan sont comparés au document actif. Si le plan est périmé,
la session le reconstruit, rafraîchit le preview et n'applique rien : un
nouveau clic explicite est requis. Cette règle garantit que ce qui est
affiché est exactement ce qui sera validé.

`StampPlacementSession` ne conserve aucun pointeur vers le document. Elle
possède uniquement le Stamp actif, la transformation temporaire, le plan,
le snapshot de preview, la clé de cache, le sous-modèle, l'ordinal de
placement et son état (`Empty`, `Active`, `Cancelled`).

### 10.3 Brush et voxel

Le preview montre les voxels exacts qui seront ajoutés, peints ou supprimés. Il utilise la couleur actuellement sélectionnée lorsque l'action en dépend.

### 10.4 Stamp

Le preview montre :

- tous les voxels du Stamp ;
- leurs couleurs réelles ;
- la rotation ;
- le miroir ;
- le pivot ;
- les collisions ;
- les positions hors limites ;
- les conflits de palette.

Pour les contenus très volumineux, un cadre peut apparaître temporairement pendant le chargement, mais la validation reste impossible tant que le Live Voxel Preview exact n'est pas prêt.

### 10.5 Collision et chevauchement

La politique officielle par défaut est `OverwriteOverlapping`.

- les collisions ne bloquent pas le placement ;
- seules les cellules chevauchées sont remplacées par les voxels du Stamp ;
- les autres voxels du document restent inchangés ;
- les chevauchements sont affichés en orange dans le Live Voxel Preview ;
- le preview reste validable si aucune autre erreur dure n'existe ;
- les positions hors limites, erreurs de format et erreurs d'allocation restent bloquantes.

Chaque changement mémorise la valeur avant et après afin qu'Undo restaure exactement les cellules remplacées.

Politiques optionnelles :

- `Reject` : tout chevauchement invalide le placement ;
- `SkipOccupied` : les cellules déjà occupées restent inchangées ;
- `OverwriteOverlapping` : politique par défaut.

Les contraintes sont des aides optionnelles. Elles ne doivent pas empêcher l'utilisateur de placer une création là où il le souhaite, tant que l'opération respecte les limites du document et les règles de sécurité.

## 11. Rotation rapide

Workflow :

```text
Maintenir R
  ↓
Molette
  ↓
Rotation par incréments
  ↓
Live Voxel Preview actualisé
```

### 11.1 Axe

- sur Workplane : normale du Workplane ;
- sur surface : normale de la surface ;
- en placement libre : axe Y monde ;
- un override futur permettra X, Y ou Z.

### 11.2 Incréments

L'incrément par défaut est 90°, afin de préserver exactement la structure voxel. Le Constraint Engine fournit l'incrément effectif.

Des réglages futurs pourront proposer 15°, 30° ou 45° lorsque le pipeline de rééchantillonnage sera explicitement accepté.

### 11.3 UX

Un indicateur temporaire dans le viewport affiche :

- axe ;
- angle ;
- incrément.

Aucun panneau ne s'ouvre. La rotation ne crée aucune entrée d'historique avant le placement. L'opération Undo/Redo contient directement la transformation finale.

## 12. Mirror rapide

`M` parcourt :

```text
None → X → Y → Z → None
```

Le miroir agit dans les axes locaux du Stamp avant sa rotation. Cette règle reste prévisible quelle que soit l'orientation dans le monde.

L'état courant est indiqué temporairement dans le viewport. Une extension future pourra autoriser `M + X`, `M + Y` ou `M + Z` pour un choix direct.

Le changement est immédiatement visible dans le Live Voxel Preview et ne produit aucune mutation documentaire avant validation.

## 13. Placement continu

Après le choix d'une création dans la Forge Library :

```text
Choisir
  ↓
Placer
  ↓
Placer
  ↓
Placer
  ↓
Échap
```

Ce comportement accélère fortement la création de murs, forêts, fenêtres, rochers ou mobilier répété.

### 13.1 Prévention des placements involontaires

- le clic ayant sélectionné la carte ne place jamais l'asset ;
- le pointeur doit entrer dans le viewport ;
- un nouveau clic gauche explicite est nécessaire ;
- aucun placement pendant une navigation caméra ;
- aucun placement sur une UI ou un champ actif ;
- un preview invalide ne peut pas être validé ;
- Échap annule immédiatement la session.

### 13.2 État conservé

Entre deux poses du même contenu, le système conserve :

- rotation ;
- miroir ;
- mode de collision ;
- contraintes ;
- préférence Smart Placement.

Rotation et miroir persistent uniquement pendant la session de placement active :

```text
Choisir
  ↓
Régler rotation et miroir
  ↓
Placer plusieurs fois
  ↓
Échap
```

Le pivot vient toujours du contenu actif. Lorsqu'un autre asset est sélectionné, le nouvel asset utilise sa transformation enregistrée lorsqu'elle existe, sinon l'état neutre. La rotation et le miroir temporaires de l'asset précédent ne sont pas transférés.

En V1, rotation et miroir ne sont pas restaurés après le redémarrage de VoxelForge Studio. Une préférence future pourra proposer cette restauration, mais elle restera optionnelle.

Chaque pose est une transaction Undo indépendante. Ctrl+Z retire donc la dernière occurrence, sans annuler toute la session.

### 13.3 Scale V1

La première version accepte uniquement une échelle `1:1`.

- le format réserve un champ de scale pour la compatibilité future ;
- `ScaleX`, `ScaleY` et `ScaleZ` doivent tous valoir exactement `1` ;
- toute autre valeur est rejetée avant le calcul du preview ;
- aucun rééchantillonnage n'est effectué ;
- aucun contrôle Scale n'est affiché dans l'interface de placement V1 ;
- une scale invalide ne produit ni mutation ni entrée Undo.

Cette décision garantit que le Live Voxel Preview et le résultat final conservent exactement les voxels du Stamp.

## 14. Smart Variants

Un groupe rassemble plusieurs créations équivalentes :

```text
Tree
├── Tree01
├── Tree02
├── Tree03
└── Tree04
```

### 14.1 Modèle

```text
StampVariantGroup
├── GroupUUID
├── Name
├── Category
├── Tags
├── PrimaryThumbnail
├── SelectionMode
├── SeedPolicy
├── Revision
└── Variants[]
    ├── VariantUUID
    ├── StampUUID
    ├── ExpectedContentHash
    ├── Weight
    ├── Enabled
    └── DisplayOrder
```

L'identifiant de variante est distinct de l'UUID du Stamp afin de permettre à un même Stamp de participer à plusieurs groupes avec des poids différents.

### 14.2 Modes

- `Fixed` : variante choisie manuellement ;
- `Sequential` : ordre stable ;
- `Random` : distribution uniforme ;
- `Weighted` : poids configurables.

#### Règles officielles du mode Weighted

Avant chaque sélection :

- les variantes manquantes sont ignorées ;
- les variantes désactivées sont ignorées ;
- les poids nuls, négatifs, non finis ou invalides sont ignorés ;
- les poids valides restants sont normalisés.

S'il reste une seule variante valide, elle est utilisée.

S'il n'en reste aucune :

- le placement est bloqué ;
- le preview devient rouge ;
- une explication claire indique qu'aucune variante valide n'est disponible ;
- aucun voxel n'est modifié ;
- aucune variante arbitraire n'est choisie ;
- aucun `PlacementOrdinal` n'est consommé.

### 14.3 Déterminisme

Le choix aléatoire doit rester stable tant que l'utilisateur déplace le même preview. Il ne doit pas changer à chaque frame.

La sélection est calculée officiellement à partir de :

```text
VariantSelectionSeed =
    Hash(GroupUUID, PlacementSessionSeed, PlacementOrdinal)
```

`PlacementSessionSeed` est créé à l'activation du groupe et reste stable pendant la session. L'utilisateur peut demander explicitement `Renew Seed` pour produire une nouvelle séquence. `PlacementOrdinal` avance uniquement après un placement validé.

La variante résolue et son UUID exact sont enregistrés dans l'opération Undo/Redo. Undo et Redo ne réalisent aucun nouveau tirage. Redo restaure exactement le contenu placé initialement.

Après validation, le compteur de placement avance et le preview suivant choisit la prochaine variante.

### 14.4 Miniature

Le groupe possède :

- une miniature principale choisie manuellement ; ou
- une composition générée à partir de plusieurs variantes.

### 14.5 Modification et suppression

- modifier un Stamp met à jour son content hash ;
- une variante manquante est marquée indisponible ;
- un groupe Fixed ne remplace jamais silencieusement une variante manquante ;
- Random et Weighted ignorent les variantes manquantes, désactivées ou invalides selon les règles officielles du mode ;
- supprimer une entrée du groupe ne supprime pas son Stamp sans confirmation distincte.

### 14.6 IA

L'IA peut demander un groupe, un mode ou une variante précise. Elle ne change jamais la variante après affichage du preview sans nouvelle proposition visible.

## 15. Smart Placement

Smart Placement propose une orientation et un pivot adaptés au contexte :

- torche vers un mur ;
- fenêtre sur une surface verticale ;
- arbre au sol ;
- élément de toiture sur une face supérieure.

### 15.1 Entrées

- pivot et normale du Stamp ;
- surface pointée et sa normale ;
- Workplane ;
- orientation locale ;
- métadonnées de support ;
- Smart Anchors futurs ;
- contraintes actives.

### 15.2 Règle produit

Smart Placement est activé par défaut en V1. Il fonctionne uniquement comme une aide non contraignante.

Il peut proposer :

- orientation ;
- pivot ;
- alignement de surface.

La proposition est visible dans le Live Voxel Preview avant toute validation.

Smart Placement ne doit jamais :

- modifier la géométrie enregistrée ;
- imposer une orientation ;
- interdire un placement normalement autorisé ;
- transformer une suggestion en contrainte cachée.

Les aides disposent de modes :

- Off ;
- Suggest ;
- Preview Assist, mode par défaut.

Une rotation manuelle verrouille temporairement l'orientation choisie par l'utilisateur.

L'aide peut être désactivée globalement dans les préférences ou temporairement pendant une session de placement. Sa désactivation ne modifie pas les données du Stamp.

## 16. Smart Anchors — extension future

Le format réserve un chunk optionnel `ANCH`.

```text
StampAnchor
├── AnchorUUID
├── Name
├── SemanticRole
├── LocalPosition
├── LocalNormal
├── LocalTangent
├── CompatibilityTags
├── Priority
└── Enabled
```

Rôles possibles :

- top ;
- bottom ;
- left ;
- right ;
- center ;
- endpoint ;
- ground ;
- wall.

Les positions utilisent le même repère fixe que le pivot. Les orientations sont stockées localement. Deux anchors sont compatibles par tags, rôle, orientation et règles d'assemblage.

Les chunks inconnus restent ignorables afin qu'une V1 puisse charger un Stamp futur sans exploiter les anchors, si sa version majeure reste compatible.

## 17. Smart Construction — extension future

Auto Repeat permettra :

```text
Choisir un élément de mur
  ↓
Tirer une distance
  ↓
Prévisualiser la répétition
  ↓
Valider la construction
```

Contraintes architecturales à prévoir :

- bounds et pivot déterministes ;
- axe de répétition explicite ;
- longueur ou pas nominal ;
- anchors de début et de fin futurs ;
- règles de raccord ;
- compatibilité Smart Variants ;
- planner capable de produire un lot d'instances ;
- preview capable d'afficher un lot ;
- opération atomique pouvant contenir plusieurs occurrences ;
- provenance de chaque occurrence conservée.

Règle Undo officielle :

```text
Appui
  ↓
Glissement
  ↓
Répétitions prévisualisées
  ↓
Relâchement
  =
Une transaction Undo atomique
```

Un seul Undo retire toute la série produite par ce geste. Un nouveau geste crée une nouvelle transaction indépendante. Aucune entrée d'historique n'est créée pendant le glissement.

Smart Construction n'est pas requise pour la première version.

## 18. Recherche et catalogue

```text
StampQuery
├── Text
├── Tags
├── Categories
├── TechnicalTypes
├── FavoriteOnly
├── Source
├── DimensionsRange
├── VoxelCountRange
├── DominantColor
├── CreatedAfter
├── VariantGroup
└── SortMode
```

La recherche :

- indexe les métadonnées sans charger les voxels ;
- normalise le texte et la casse ;
- pondère le nom et les tags ;
- propose filtres et facettes ;
- trie par pertinence, récent, nom, taille ou utilisation ;
- accepte catégories et sous-dossiers personnalisés.

### 18.1 Backend officiel V1

Le catalogue V1 est simple, local, versionné et atomique.

- un format JSON versionné est recommandé ;
- l'écriture utilise un fichier temporaire, une validation puis une installation atomique ;
- le catalogue ne dépend ni d'ImGui ni du renderer ;
- Project Library et My Library possèdent des catalogues indépendants ;
- les fichiers `.vfstamp` et autres assets restent la source de vérité ;
- le catalogue est un index dérivé et peut être entièrement reconstruit ;
- une corruption du catalogue ne doit jamais entraîner la suppression d'un asset ;
- SQLite n'est pas imposé en V1.

Le service dépend d'une interface `IStampCatalogRepository`. Une implémentation JSON V1 peut donc être remplacée plus tard par SQLite, un catalogue Cloud ou un plugin sans modifier `StampLibraryService`, la recherche ou l'UI.

La reconstruction parcourt séparément les racines Project et User autorisées, valide les assets, relit leurs métadonnées, régénère les entrées et signale les collisions d'UUID sans choisir silencieusement un gagnant. Les chemins stockés dans le catalogue Project restent relatifs au Project Root.

## 19. Format `.vfstamp`

La décision V1 est conservée : un conteneur chunké versionné.

```text
VFSTAMP Header
├── Magic "VFSTAMP"
├── MajorVersion
├── MinorVersion
├── Flags
└── ChunkDirectory

Chunks
├── MANF  Manifest JSON UTF-8
├── PAL0  Palette locale
├── VOX0  Voxels triés et binaires
├── THMB  Miniature optionnelle
├── ANCH  Smart Anchors futurs
├── HASH  Intégrité
└── EXTN  Extensions namespacées
```

Règles :

- version majeure inconnue : rejet ;
- version mineure récente : acceptation si les chunks obligatoires sont compris ;
- chunks inconnus : ignorés ;
- checksum vérifié ;
- tailles validées avant allocation ;
- doublons de positions rejetés ;
- références palette invalides rejetées ;
- écriture via temporaire, vérification puis installation atomique ;
- import VOX possible ;
- export VOX possible avec avertissement sur les métadonnées perdues.

## 20. Performances

### 20.1 Chargement

- métadonnées seules au démarrage ;
- payload voxel chargé à la demande ;
- miniatures chargées paresseusement ;
- chargements annulables ;
- surveillance filesystem avec debounce.

### 20.2 Cache

- cache LRU des payloads ;
- budget mémoire configurable ;
- miniature indexée par UUID et content hash ;
- aucune lecture fichier par frame ;
- invalidation lorsque le hash change.

### 20.3 Preview

Le placement est recalculé uniquement si changent :

- cellule cible ;
- rotation ;
- miroir ;
- pivot ;
- variante ;
- politique de collision ;
- palette cible ;
- révision du document.

Sinon les buffers précédents sont réutilisés.

Complexité :

- capture : `O(voxels sélectionnés)` ;
- transformation : `O(voxels du Stamp)` ;
- collision : `O(voxels du Stamp)` sur le document sparse ;
- recherche : métadonnées uniquement.

### 20.4 Limites centrales V1

Les limites appartiennent à une politique centrale configurable, par exemple `StampResourceLimits`. Elles ne sont pas dispersées dans le serializer, le catalogue et la preview.

Valeurs initiales prudentes proposées :

| Limite | Seuil souple V1 | Limite dure V1 |
|---|---:|---:|
| Nombre de voxels | 262 144 | 2 097 152 |
| Dimension par axe | 64 | 512 |
| Taille décodée estimée | 32 Mio | 256 Mio |
| Taille du fichier | 16 Mio | 128 Mio |
| Nombre de chunks | avertissement à 32 | 64 |

Le seuil souple :

- autorise l'opération ;
- affiche un avertissement ;
- fournit nombre de voxels, mémoire estimée, coût de preview et taille de fichier ;
- demande une confirmation explicite pour la sauvegarde ou le chargement.

La limite dure :

- est vérifiée avant allocation, décompression ou multiplication de tailles ;
- refuse l'opération sans mutation ni fichier partiel ;
- protège contre overflow, allocation abusive et fichier hostile.

Ces valeurs sont des constantes de politique V1, modifiables après benchmarks sans migration du format. Un fichier peut déclarer des dimensions supérieures, mais une version de l'application dont la politique ne les accepte pas doit le refuser proprement.

## 21. Forge Academy

Forge Academy utilise l'interface réelle, sans interface parallèle.

Un futur modèle déclaratif peut contenir :

```text
AcademyLesson
├── LessonId
├── Title
└── Steps[]
    ├── TargetUiId
    ├── CommandId
    ├── Instruction
    ├── ExpectedState
    └── CanReplay
```

Le tutoriel Voxel Stamps explique :

1. sélectionner ;
2. utiliser `Save Selection As...` ;
3. retrouver la création dans Forge Library ;
4. placer plusieurs exemplaires ;
5. utiliser R + molette ;
6. utiliser M ;
7. créer un groupe Smart Variants.

Les leçons sont relançables depuis une commande d'aide contextuelle. Elles ne modifient jamais directement le document.

## 22. IA future

L'IA travaille avec des descriptions et des recettes de placement :

```text
StampPlacementRecipe
├── StampUUID ou GroupUUID
├── VariantUUID optionnel
├── ExpectedContentHash
├── Position
├── Rotation
├── Mirror
├── Pivot
├── Constraints
└── Explanation
```

Elle peut :

- rechercher une création ;
- proposer une variante ;
- sélectionner une variante dans un groupe ;
- préparer un placement ;
- afficher un preview.

Elle ne peut jamais :

- modifier silencieusement le contenu de l'artisan ;
- remplacer une variante après validation visuelle ;
- écrire directement dans la scène ;
- contourner les collisions, contraintes ou l'historique.

Pipeline permanent :

```text
L'IA propose
  ↓
Live Voxel Preview
  ↓
L'artisan décide
  ↓
Transaction standard
```

## 23. Compatibilité future

Points d'extension requis :

| Système futur | Point d'extension |
|---|---|
| Prefabs | Références d'assets, hiérarchie et composants |
| Matériaux | Identifiant local de matériau par voxel |
| Animation | Chunks namespacés et timeline externe |
| Rigging | Repère local et anchors stables |
| Scenes | Références UUID + content hash |
| Smart Anchors | Chunk `ANCH` |
| Smart Construction | Batch placement plan |
| Forge Academy | IDs UI et commandes stables |
| AI Composer | Requêtes catalogue et PlacementRecipe |
| Plugins | Extensions namespacées et handlers déclarés |

Le Stamp Core ne doit dépendre d'aucun de ces systèmes.

## 24. Roadmap

### Fondations préalables

1. contrat de Preview Engine commun ;
2. Selection stable et snapshotable ;
3. transaction document capable d'inclure palette et voxels ;
4. format Stamp et validation ;
5. politique centrale de limites souples et dures ;
6. sérialisation atomique ;
7. repository de catalogue abstrait et backend JSON reconstructible ;
8. placement planner avec `OverwriteOverlapping` par défaut ;
9. résolution déterministe Auto Pivot et Smart Variants ;
10. intégration Undo/Redo.

### Progression fonctionnelle

| Étape | Contenu | Complexité indicative |
|---|---|---|
| 1. Stamp Core | Domaine, validation, UUID, palette, limites, Scale 1:1 | Moyenne |
| 2. Save Selection As... | Capture, Auto Pivot déterministe, écriture | Moyenne |
| 3. Catalogue minimal | JSON atomique, catalogues Project/User séparés, reconstruction | Moyenne |
| 4. Live Stamp Preview | Preview exacte, chevauchements orange, overwrite | Élevée |
| 5. Placement continu | Session, transformations temporaires, transactions indépendantes | Moyenne |
| 6. Rotation et miroir rapides | R + molette, M | Moyenne |
| 7. Forge Library Integration | Cartes, recherche, catégories | Élevée |
| 8. Smart Variants | Groupes, Weighted sûr, seed reproductible, Undo/Redo stable | Élevée |
| 9. Smart Placement | Preview Assist activé par défaut, désactivable et non contraignant | Élevée |
| 10. Préparation Prefabs | Contrats et références | Moyenne |
| 11. Extensions | Anchors, Auto Repeat atomique, Academy, IA | Très élevée |

Smart Anchors et Smart Construction ne bloquent pas la première version.

## 25. Matrice de risques et de tests

| Cas | Risque | Comportement attendu | Tests |
|---|---|---|---|
| Sélection vide | Asset sans contenu | Commande désactivée, erreur domaine si appelée | Unitaire + UI |
| Très grande sélection | Mémoire et latence | Limite explicite, progression, aucune mutation partielle | Performance |
| Coordonnées négatives | Débordement ou mauvais offset | Normalisation locale déterministe | Round-trip |
| Palette invalide | Couleurs corrompues | Rejet avant preview | Unitaire format |
| Pivot hors limites | Placement incohérent | Rejet ou correction explicite avant sauvegarde | Unitaire |
| Asset supprimé | Référence morte | Carte Missing, retrait ou relocalisation explicite | Intégration |
| Asset déplacé | Chemin obsolète | Résolution par UUID/hash, catalogue réparé | Intégration |
| Catalogue corrompu | Bibliothèque indisponible | Sauvegarde, reconstruction depuis les fichiers | Recovery |
| Version future inconnue | Lecture incorrecte | Règles major/minor respectées | Compatibilité |
| Variante manquante | Preview erroné | Avertissement, jamais de remplacement silencieux | Unitaire |
| Collision UUID | Mauvais contenu chargé | Comparaison hash, résolution explicite | Import |
| Placement répété | État ou historique incorrect | Une transaction par pose | Smoke |
| Rotation + miroir | Géométrie divergente | Ordre local mirror puis rotation, preview exact | Combinatoire |
| Undo/Redo multiple | Ordre incorrect | Retrait/restauration pose par pose | Intégration |
| Changement de projet | Références croisées | Session annulée, caches et catalogue changés | Smoke |
| Fermeture pendant écriture | Fichier partiel | Temporaire, rollback, catalogue cohérent | Fault injection |
| Preview volumineux | Chute de FPS | Cache, calcul sur changement, buffers réutilisés | Benchmark |
| Compatibilité ascendante | Perte de données | Chunks inconnus ignorés, obligatoire validé | Golden files |
| Random Variants | Flicker | Variante stable pendant le preview | Unitaire |
| Suppression de variante | Groupe invalide | État Missing visible | Intégration |
| Palette pleine | Couleur impossible | Placement refusé, aucune approximation | Unitaire |
| Preview vs commit | Résultat différent | Comparaison voxel par voxel | Test de contrat |
| Plan de placement périmé | Commit différent du preview affiché | Rebuild du plan et du preview, aucun commit automatique, second clic requis | Intégration + smoke |
| Cache de placement | Recalcul inutile ou réutilisation d'un résultat invalide | Clé Stamp + document + génération + révision + sous-modèle + transform + politique | Unitaire |
| Auto Pivot répété | Résultat instable | Même contexte et même politique produisent le même preset, pivot et normale | Test déterministe |
| Surface horizontale/verticale | Mauvais preset Auto | Horizontal supérieur = Bottom Center, vertical = Surface | Test de décision |
| Placement libre ou grille | Pivot incorrect | Libre/ambigu = Center, grille précise = Corner | Test de décision |
| Chevauchement par défaut | Placement bloqué | Placement autorisé, cellules remplacées, preview orange | Intégration |
| Undo d'un overwrite | Données source perdues | Toutes les cellules chevauchées retrouvent leur valeur précédente | Undo/Redo |
| Reconstruction catalogue | Index irrécupérable | Catalogue supprimé/corrompu reconstruit depuis les assets | Recovery |
| Catalogue et assets divergents | Perte d'asset | Les fichiers restent source de vérité | Intégration |
| Smart Variant reproductible | Séquence instable | GroupUUID + SessionSeed + Ordinal donnent la même variante | Test déterministe |
| Undo/Redo d'une variante | Nouveau tirage | UUID exact conservé, aucun tirage pendant Undo/Redo | Intégration |
| Renew Seed | Séquence inchangée | Une action explicite crée une nouvelle séquence stable | Unitaire |
| Scale différente de 1 | Rééchantillonnage implicite | Rejet avant preview et sans historique | Unitaire |
| Seuil souple dépassé | Refus excessif | Avertissement et estimation, opération autorisable | Limites |
| Limite dure dépassée | Allocation dangereuse | Rejet avant allocation et sans mutation | Sécurité |
| Overflow de dimensions | Dépassement entier | Calcul vérifié et rejet déterministe | Sécurité |
| Référence Project vers My Library | Chemin absolu non portable | Référence refusée ou import explicite dans Project Library | Portabilité |
| Reconstruction des deux catalogues | Mélange des scopes | Project et User reconstruits indépendamment depuis leurs assets | Recovery |
| Rotation/miroir pendant placement | État perdu trop tôt | État conservé jusqu'à Échap pour l'asset actif | Intégration |
| Changement d'asset | Transformation héritée involontaire | Transformation enregistrée du nouvel asset ou état neutre | Intégration |
| Redémarrage application | État temporaire restauré en V1 | Rotation et miroir reviennent à l'état neutre | Session |
| Weighted partiellement invalide | Distribution incorrecte | Entrées invalides ignorées et poids restants normalisés | Unitaire |
| Weighted sans variante valide | Choix arbitraire | Preview rouge, placement bloqué, aucun voxel ni ordinal consommé | Intégration |
| Smart Placement par défaut | Aide absente ou bloquante | Preview Assist actif, proposition visible, placement manuel toujours autorisé | UX + intégration |
| Désactivation Smart Placement | Aide persistante | Désactivation globale ou temporaire sans modifier le Stamp | Intégration |
| Brush Profile dans Smart Tool | Surcharge de CREATE | Brush visible uniquement dans Forge Library/Brushes | UI |
| Auto Repeat, un geste | Historique fragmenté | Toute la série appartient à une seule transaction | Undo/Redo |
| Auto Repeat, deux gestes | Transactions fusionnées | Deux opérations Undo indépendantes | Undo/Redo |

## 26. Décisions finales et ambiguïtés restantes

### 26.1 Décisions officielles

| Sujet | Décision |
|---|---|
| Auto Pivot | Auto par défaut, déterministe : horizontal supérieur = Bottom Center, vertical = Surface, libre/ambigu = Center, grille précise = Corner |
| Collision | `OverwriteOverlapping` par défaut, chevauchements orange, Reject et SkipOccupied optionnels |
| Limites | Politique centrale, seuils souples avec estimation, limites dures avant allocation |
| Catalogue | JSON local versionné et atomique en V1, assets source de vérité, reconstruction totale, repository abstrait |
| Smart Variants | Seed dérivé de GroupUUID + PlacementSessionSeed + PlacementOrdinal, UUID exact conservé par Undo/Redo |
| Scale V1 | Échelle 1:1 uniquement, toute autre valeur refusée |
| Bibliothèques | Project Library partageable et My Library utilisateur sont séparées ; aucune dépendance absolue du projet vers My Library |
| Rotation et miroir | Persistants uniquement pendant la session de placement de l'asset actif ; non restaurés au redémarrage en V1 |
| Weighted invalide | Variantes invalides ignorées et poids renormalisés ; aucune variante valide bloque le placement avec preview rouge |
| Smart Placement | Preview Assist activé par défaut, non contraignant et désactivable |
| Brushes | Vue fonctionnelle Forge Library séparée des Creations ; aucun Brush Profile dans Smart Tool |
| Auto Repeat | Un geste complet produit une seule transaction Undo atomique |

### 26.2 Ambiguïtés restantes

Aucune ambiguïté bloquante ne reste pour commencer l'implémentation du Stamp Core V1.

Les valeurs de seuils souples et durs pourront être ajustées après benchmarks, conformément à la politique centrale, sans modifier les décisions d'architecture ni le format.

## 27. Conclusion

Voxel Stamps n'est pas un copier-coller enrichi. C'est une fondation de contenu réutilisable reliant Selection, Forge Library, Live Voxel Preview, placement atomique, variantes et extensions futures.

L'utilisateur crée une fois, classe sa création puis la réemploie avec un contrôle complet sur le pivot, l'orientation, le miroir, la variante et le moment de validation.

Le système accélère les tâches répétitives, mais ne choisit jamais silencieusement à la place de l'artiste.

> Créer plus vite. Rester l'artisan.
