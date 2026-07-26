# VF-0300 — Smart Tool Architecture

| Champ | Valeur |
|---|---|
| Projet | VoxelForge Studio |
| Statut | Proposition d'architecture — validation Tony requise |
| Version du document | 1.0 |
| Date | 2026-07-26 |
| Implémentation cible | Non commencée |
| Principe produit | Créer plus vite. Rester l'artisan. |

## 0. Résumé exécutif

Le Smart Tool est l'outil principal de création voxel de VoxelForge Studio. Il
ne constitue ni une collection de boutons indépendants, ni un nouveau moteur
de document, ni une façade contenant toute la logique de l'éditeur. Il combine
trois décisions orthogonales :

1. **Action** — ce que l'utilisateur veut faire aux cellules ;
2. **Mode** — comment la région concernée est désignée ;
3. **Brush** — quelle empreinte locale est appliquée à cette région.

La Toolbar permanente reste limitée à :

- Smart Tool ;
- Selection ;
- Transform.

Les actions, modes, formes et paramètres sont contextuels. Ils restent dans le
panneau du Smart Tool ou dans un menu contextuel, sans proliférer dans la
Toolbar.

L'architecture officielle reprend la propriété la plus importante éprouvée par
les Voxel Stamps :

```text
SmartToolSession
        |
        v
SmartToolPlanner
        |
        v
SmartToolPlan immuable
        |                         |
        v                         v
SmartToolPreviewAdapter   SmartToolCommitAdapter
        |                         |
        v                         v
Preview Engine             VoxelEditHistory
```

Le plan immuable est la seule source de vérité d'un geste. Le preview et le
commit consomment exactement les mêmes changements résolus. Le clic final ne
recalcule ni géométrie, ni couleur, ni collision. Si la révision du document a
changé, le plan devient périmé : il est reconstruit et affiché, mais n'est pas
appliqué sans une nouvelle validation explicite.

Cette architecture est adaptée au Smart Tool, avec une différence importante
par rapport aux Stamps : un Stamp transforme une source voxel fixe, tandis que
le Smart Tool résout une intention interactive pouvant dépendre d'un hit, d'une
face connectée, de plusieurs points, d'un Workplane ou d'un flood. Les
résolveurs de mode sont donc des stratégies du Planner, pas des Stamp
miniatures.

Décisions structurantes :

- `Fill` est un **mode de sélection de région**, pas une action métier
  supplémentaire ;
- Face est un mode de première classe ;
- Line est un mode de première classe ;
- Face Add avec épaisseur fournit l'extrusion voxel V1 ; une commande
  interactive « Extrude » dédiée viendra en V1.1 ;
- Face Inset et Face Fill restent futurs ;
- les chevauchements et la Construction Box sont non bloquants par défaut ;
- les limites du format de document et les erreurs de sécurité restent
  bloquantes ;
- chaque geste validé produit une opération Undo/Redo atomique ;
- les profils mémorisent des réglages, jamais un état de document ou
  d'interaction.

## 1. Sources et état réel inspecté

Cette architecture est fondée sur le dépôt actuel, et non sur un modèle
théorique.

### 1.1 Documents de référence

- `AGENTS.md`
- `docs/000_Foundation/VF-0001-Constitution.md`
- `docs/000_Foundation/ADR/ADR-0003-Artist-Control.md`
- `docs/100_Architecture/Scene/VF-0220-Scene-Foundation.md`
- `docs/100_Architecture/Editor/VF-0250-Voxel-Stamps-Architecture-V2.md`
- `docs/100_Architecture/Editor/VF-0251-Voxel-Stamps-Implementation-Plan-V1.md`
- future `VF-0290 — VoxelForge Design System`
- future `VF-0291 — Icon Pack Production`

VF-0290 et VF-0291 n'existent pas encore dans le dépôt. VF-0300 définit leurs
besoins fonctionnels sans inventer leur contenu visuel final.

### 1.2 Composants actuels inspectés

| Zone | Composants observés | État utile |
|---|---|---|
| État Smart Tool | `SmartTool`, `SmartGeometry`, `SmartAction` | état unique présent, Add/Erase/Paint opérationnels |
| Géométrie Brush | `SmartBrushEngine`, `SmartBrushState` | Cube/Sphere, Volume3D/Surface2D, orientations, taille 1–16 |
| Application | `VoxelPencilTool`, `VoxelPaintBrushTool` | transactions atomiques multi-voxel existantes |
| Preview Smart Brush | `SmartBrushPreviewResolver`, `SmartBrushPreviewCache` | Ghost Voxels, cache par document/révision/état |
| Preview générique | `VoxelPreviewData`, `VoxelPreviewSession` | snapshot renderer-neutral déjà éprouvé par Stamps |
| Historique | `VoxelEditOperation`, `VoxelEditHistory` | opération atomique, palette composite, Undo/Redo |
| Mutation | `ApplyVoxelEditOperation` | document + grilles de compatibilité + palette + rebuild |
| Outils historiques | `VoxelBoxService`, `VoxelLineService`, `VoxelSphereService`, `VoxelFillService` | capacités réutilisables, encore parallèles |
| Interaction | `EditorWorkspace`, `EditorInputService`, `VoxelToolState` | routage central, forte accumulation de responsabilités |
| Picking | `VoxelRaycast`, `VoxelSelectionState` | hit, face, normale, adjacent, révision |
| Workplane | `WorkplaneService` | Workplane Y=0 V1, intersection déterministe |
| Palette | `PaletteService`, `PaintPaletteSelection` | couleur active, récents, synchronisation document |
| UI | `ToolContext`, `SmartToolPanel`, `SmartBrushOptions` | panneau contextuel et Toolbar à trois boutons |
| Profils locaux | `BrushProfileService` et tests non commités | CRUD JSON V1, favoris, récents, validation |

### 1.3 Limitations actuelles

1. `SmartAction` et `SmartBrushMode` représentent deux fois la même intention.
2. `SmartGeometry` mélange mode d'interaction et forme de Brush.
3. Le preview et l'application rappellent le même moteur, mais ne consomment
   pas encore un plan immuable commun.
4. `SmartBrushPreviewResolver` et `VoxelPreviewData` forment deux contrats de
   preview parallèles.
5. `VoxelToolState` conserve Pencil, Eraser, Fill, Box, Line et Sphere comme
   outils principaux historiques.
6. `EditorWorkspace` choisit l'action, construit le contexte, déclenche
   l'application et reconstruit la présentation.
7. Cube et Sphere sont implémentés à la fois comme formes locales de Brush et
   comme anciens outils de construction.
8. Les previews agrégés des grosses brushes peuvent devenir une boîte ou une
   sphère approximative ; ce mode n'est pas suffisant pour une validation
   multi-voxel lorsque l'exactitude cellule par cellule est requise.
9. Le Workplane V1 est limité à Y=0 et les dimensions du document restent des
   limites dures techniques.
10. Aucun Face Tool n'existe encore.
11. Line existe, mais hors du Smart Tool et sans Brush le long du trajet.
12. Les Brush Profiles locaux sont prometteurs mais ne couvrent que Pencil,
    Cube/Sphere, Add/Erase/Paint et des réglages limités.

Ces éléments sont une base de migration, pas une raison de créer un second
système parallèle.

## 2. Définition et frontières du Smart Tool

### 2.1 Responsabilité

Le Smart Tool transforme une interaction explicite de l'utilisateur en un plan
de modifications voxel exact, prévisualisable et annulable.

Il est responsable de :

- conserver les réglages actifs ;
- gérer le cycle de vie du geste ;
- demander le contexte Scene courant ;
- résoudre un ou plusieurs points d'entrée ;
- construire un plan déterministe ;
- présenter ce plan ;
- valider ce plan via l'historique officiel.

### 2.2 Ce qu'il remplace

Après migration, le Smart Tool remplace comme outils principaux :

- Pencil ;
- Eraser ;
- Paint ;
- Fill ;
- Box ;
- Line ;
- Sphere.

Leurs services de calcul peuvent être réutilisés ou adaptés pendant la
migration. Leurs boutons permanents et leurs états parallèles disparaissent.

### 2.3 Ce qu'il ne devient pas

Le Smart Tool ne devient pas :

- Selection ;
- Transform ;
- Scene ;
- Construction Box ;
- Workplane ;
- navigateur Forge Library ;
- renderer ;
- historique Undo/Redo ;
- format de fichier ;
- moteur d'IA ;
- collection de Build Tools cachés.

### 2.4 Relations avec Selection et Transform

- Selection produit ou modifie une région persistante sélectionnée.
- Transform déplace, tourne ou redimensionne cette sélection.
- Smart Tool modifie directement des cellules selon un geste de création.

Un futur mode peut lire la sélection comme masque optionnel, mais le Smart Tool
ne possède pas la sélection et ne change pas son contrat.

## 3. Modèle de domaine

### 3.1 Types canoniques

```text
SmartToolSettings
├── Action
├── Mode
├── Brush
├── PalettePolicy
├── SnapPolicy
├── OverwritePolicy
├── PreviewPolicy
└── AdvancedOptions

SmartToolInput
├── Pointer samples
├── Ray hits
├── Face identifiers
├── Anchors
├── Modifiers
└── Gesture phase

SmartToolSceneContext
├── Document identity/generation/revision
├── Target sub-model
├── Active grid
├── Active Workplane
├── Scene origin and axes
├── Reference dimensions
└── Construction diagnostics policy
```

Les noms C++ définitifs peuvent évoluer, mais les concepts ne doivent plus être
fusionnés.

### 3.2 Action

`SmartAction` décrit uniquement la mutation :

- Add ;
- Remove ;
- Paint ;
- Replace ;
- Pick.

`Fill` n'est pas retenu comme action canonique. Il décrit comment une région
est trouvée, et appartient donc à `SmartMode::Fill`. Cette décision évite les
combinaisons ambiguës telles que « Action Fill + Mode Line ».

Actions futures :

- Smooth ;
- Noise ;
- Material Paint.

`Pattern` décrit une distribution ou une Brush, pas une mutation. Il ne devient
pas une action.

### 3.3 Mode

`SmartMode` décrit comment les cellules candidates sont obtenues :

- SingleVoxel ;
- Brush ;
- Face ;
- Line ;
- Rectangle ;
- Plane ;
- Box ;
- Circle ;
- Sphere ;
- Cylinder ;
- Fill ;
- Surface.

Modes futurs :

- Cone ;
- Diamond ;
- Polyline ;
- Chain ;
- Flood avancé ;
- Shell ;
- CustomBrush ;
- StampBrush.

### 3.4 Brush

`SmartBrushDefinition` décrit une empreinte locale :

- Shape ;
- Width ;
- Height ;
- Depth ;
- Orientation ;
- Pivot ;
- Spacing ;
- optional falloff futur ;
- optional custom source futur.

Une Brush ne décide ni de l'action ni du document cible.

### 3.5 Identité et révision

Chaque plan contient :

- identité process-local du document ;
- génération de session ;
- révision du document ;
- index du sous-modèle ;
- identifiant du geste ;
- version des réglages ;
- version du contexte Scene.

Cette identité interdit l'application silencieuse d'un plan construit pour un
autre état.

## 4. Architecture proposée

### 4.1 Vue générale

```text
Input / Tool Panel / Shortcut
             |
             v
      SmartToolController
             |
             v
       SmartToolSession
             |
     +-------+-------+
     |               |
     v               v
SceneContext     PaletteContext
     |               |
     +-------+-------+
             |
             v
      SmartToolPlanner
             |
     +-------+--------+----------------+
     |                |                |
     v                v                v
ModeResolver    BrushResolver    ActionResolver
     |                |                |
     +-------+--------+----------------+
             |
             v
      SmartToolPlan (immutable)
             |
       +-----+------+
       |            |
       v            v
PreviewAdapter   CommitAdapter
       |            |
       v            v
Preview Engine   VoxelEditHistory
```

### 4.2 Dépendances

```text
UI / Input
    ↓
Application / Session
    ↓
Smart Tool Domain
    ↓
Document contracts
```

Le domaine Smart Tool ne dépend de :

- ni ImGui ;
- ni viewport ;
- ni renderer ;
- ni docking ;
- ni `EditorWorkspace` ;
- ni SDL ;
- ni D3D12.

Le renderer ne dépend que de données de preview préparées.

### 4.3 SmartToolController

Responsabilités :

- traduire les commandes UI et input en intentions ;
- ouvrir, mettre à jour, valider ou annuler une session ;
- demander un nouveau plan lorsque la clé change ;
- transmettre le plan au preview ;
- demander au CommitAdapter d'appliquer le plan.

Il ne calcule aucune position.

### 4.4 SmartToolSession

État temporaire :

- réglages actifs ;
- phase du geste ;
- points et échantillons validés ;
- face ou Workplane d'ancrage ;
- plan courant ;
- clé de cache ;
- profil actif par identifiant ;
- avertissements acquittés pour le geste.

La session ne conserve aucun pointeur durable vers le document. Elle est
invalidée lors d'un changement de document, de génération, de sous-modèle ou de
pilier actif.

### 4.5 SmartToolPlanner

Le Planner est :

- indépendant de l'UI ;
- déterministe ;
- testable sans renderer ;
- la seule autorité de résolution ;
- sans mutation du document ;
- sûr face aux overflows et allocations.

Il orchestre trois familles de stratégies :

1. `IModeResolver` — génère un domaine ou une trajectoire ;
2. `IBrushResolver` — produit l'empreinte locale ;
3. `IActionResolver` — transforme les candidats en changements avant/après.

Ces interfaces sont internes au domaine. L'API publique principale reste une
opération de planification unique afin d'éviter que les outils connaissent les
stratégies concrètes.

### 4.6 Réutilisation du code actuel

| Existant | Décision |
|---|---|
| `SmartBrushEngine` | migrer progressivement vers `IBrushResolver`; ne pas dupliquer |
| `VoxelLineService::CalculatePositions` | extraire/adapter l'algorithme dans le résolveur Line |
| `VoxelBoxService::CalculateBounds` | réutiliser les règles de bounds |
| `VoxelSphereService::CalculatePositions` | comparer puis consolider avec Sphere Brush |
| `VoxelFillService` | extraire la résolution de région sans mutation |
| `VoxelEditHistory` | conserver comme frontière d'application |
| `ApplyVoxelEditOperation` | conserver comme mutation atomique |
| `VoxelPreviewSession` | étendre/adopter comme contrat commun |
| `SmartBrushPreviewCache` | remplacer par cache de `SmartToolPlan`, puis retirer |
| `WorkplaneService` | consommer via le contrat Scene |

## 5. Contrat du plan immuable

### 5.1 Contenu minimal

```text
SmartToolPlan
├── PlanId
├── DocumentIdentity
├── DocumentGeneration
├── DocumentRevision
├── SceneContextVersion
├── TargetSubModel
├── GestureId
├── SettingsSnapshot
├── InputsSnapshot
├── Anchors
├── FaceData optional
├── ResolvedCells
│   ├── Position
│   ├── Before
│   ├── After
│   ├── PreviewState
│   └── SourceOrdinal
├── Bounds
├── Statistics
├── Diagnostics
├── EstimatedMemory
├── CanCommit
└── CacheKey
```

`Before` et `After` sont explicites. Le preview sait donc ce qui sera ajouté,
retiré, peint, remplacé ou ignoré.

### 5.2 Immutabilité

Après construction, aucune collection du plan ne peut être modifiée. Une
interaction différente produit un nouveau plan. L'implémentation peut employer
un objet valeur déplacé ou un `shared_ptr<const SmartToolPlan>`, mais ne doit
pas exposer de mutation partagée.

### 5.3 Clé de cache

La clé couvre tout ce qui modifie le résultat :

- identité/génération/révision du document ;
- sous-modèle ;
- action ;
- mode ;
- Brush complète ;
- points d'entrée ;
- hit/face/normale ;
- Workplane et grille ;
- palette active et source Replace ;
- snap ;
- overwrite policy ;
- masque Selection éventuel ;
- politique de limites ;
- version des algorithmes.

Même clé = même plan. Toute différence significative = nouveau plan.

### 5.4 Validation au commit

Le CommitAdapter vérifie :

- identité ;
- génération ;
- révision ;
- sous-modèle ;
- `CanCommit` ;
- intégrité du plan.

Il ne relit pas les voxels pour recalculer le résultat. Si l'identité ou la
révision a changé, il refuse ce plan, demande un refresh et exige une nouvelle
validation utilisateur.

## 6. Actions métier

### 6.1 Sémantique

| Action | Voxel vide | Voxel existant | Palette | Undo/Redo |
|---|---|---|---|---|
| Add | crée avec la couleur active | inchangé par défaut | couleur active requise | restaure l'état vide |
| Remove | inchangé | supprime | aucune couleur requise | restaure valeur et couleur |
| Paint | inchangé | remplace la couleur si différente | couleur active requise | restaure la couleur précédente |
| Replace | inchangé | remplace seulement si la règle source correspond | source + destination requises | restaure chaque couleur précédente |
| Pick | aucune mutation | choisit couleur/matériau | met à jour le contexte palette | aucune entrée d'historique |

L'occupation n'est pas une erreur pour Add : une cellule déjà occupée est
ignorée et signalée orange. Cette règle respecte la création non bloquante sans
transformer Add en Replace.

### 6.2 Replace

Replace possède une règle source explicite :

- index palette source ;
- couleur exacte future ;
- matériau futur ;
- ensemble de catégories futur.

En V1, seul l'index palette source est retenu. La source peut être choisie par
Pick ou par un contrôle dédié. Replace sans source valide ne peut pas être
commité et affiche un diagnostic explicite.

### 6.3 Pick

Pick est une action de contexte :

- un clic ;
- aucun preview multi-voxel ;
- aucun changement document ;
- aucune transaction ;
- met à jour la palette ou le matériau actif ;
- revient à l'action précédente si utilisé comme raccourci temporaire.

### 6.4 Fill

La notion « Fill action » a été étudiée puis rejetée comme redondante. Le
comportement utile est :

```text
Action = Paint / Remove / Replace
Mode = Fill
```

Un Add Fill d'espace vide n'entre pas dans V1 : sans frontière explicite, il
peut sélectionner un volume immense ou tout l'extérieur du modèle.

### 6.5 Actions futures

- Noise modifie une propriété selon une seed enregistrée dans le plan ;
- Smooth applique une règle voxel déterministe et réversible ;
- Flatten projette ou tronque une région vers un plan de référence explicite ;
- Material Paint modifie le matériau lorsque ce domaine existera.

Random n'est pas une action générique : il devient une option déterministe
d'une Brush ou d'une action, avec seed. L'IA propose des réglages ou un plan,
mais ne valide jamais à la place de l'utilisateur.

## 7. Matrice Action × Mode

Légende : `V1`, `V1.1`, `F` futur, `—` non pertinent.

| Mode | Add | Remove | Paint | Replace | Pick |
|---|---:|---:|---:|---:|---:|
| Single Voxel | V1 | V1 | V1 | V1 | V1 |
| Brush | V1 | V1 | V1 | V1 | — |
| Face | V1 | V1 | V1 | V1 | V1 via hit |
| Line | V1 | V1 | V1 | V1 | — |
| Rectangle | V1 | V1 | V1 | V1 | — |
| Plane | V1.1 | V1.1 | V1.1 | V1.1 | — |
| Box | V1.1 | V1.1 | V1.1 | V1.1 | — |
| Circle | V1.1 | V1.1 | V1.1 | V1.1 | — |
| Sphere | V1.1 | V1.1 | V1.1 | V1.1 | — |
| Cylinder | V1.1 | V1.1 | V1.1 | V1.1 | — |
| Fill | — | V1 | V1 | V1 | — |
| Surface | F | F | F | F | — |
| Cone | F | F | F | F | — |
| Diamond | F | F | F | F | — |
| Polyline | F | F | F | F | — |
| Chain | F | F | F | F | — |
| Shell | F | F | F | F | — |
| Custom Brush | F | F | F | F | — |
| Stamp Brush | F | F | F | F | — |

## 8. Interactions des modes

### 8.1 Single Voxel

- entrée : hit voxel ou Workplane ;
- interaction : un clic ;
- ancrage : cellule hit pour Remove/Paint/Replace/Pick, cellule adjacente pour Add ;
- preview : une cellule exacte ;
- commit : au clic ;
- annulation : aucune interaction persistante, Esc désactive le Smart Tool ou
  annule un état temporaire.

### 8.2 Brush

- entrée : hit ou Workplane ;
- interaction : clic, puis drag continu optionnel ;
- ancrage : pivot de Brush projeté sur la surface ;
- snap : cellule, grille ou axe via Scene ;
- preview : empreinte exacte à la cellule cible ;
- commit clic : une transaction ;
- commit drag : une transaction par geste complet en V1 ;
- annulation : Esc ou capture perdue avant validation.

La trajectoire du drag est échantillonnée par distance en espace voxel, pas par
fréquence des événements souris. L'espacement appartient aux réglages.

### 8.3 Face

Voir le chapitre 9. Un hit identifie une face connectée ; l'épaisseur est
appliquée le long de sa normale.

### 8.4 Line

- premier clic : point A ;
- déplacement : preview exact A → curseur ;
- second clic ou Enter : validation ;
- Esc : annulation ;
- drag futur optionnel, sans changer le résolveur.

### 8.5 Rectangle

- premier clic : coin A et plan de construction ;
- déplacement : coin B ;
- second clic : validation ;
- mode contour ou rempli explicite ;
- épaisseur optionnelle le long de la normale ;
- Esc annule.

### 8.6 Plane

Plane généralise Rectangle à un plan orienté et à des dimensions numériques.
Il reste V1.1 pour éviter de dupliquer Rectangle et Face avant stabilisation.

### 8.7 Box, Circle, Sphere, Cylinder

Interactions à deux points :

- point d'ancrage ;
- point de dimension ;
- preview pendant le déplacement ;
- validation au second clic ;
- dimensions éditables numériquement ;
- une seule transaction.

Les anciens services servent de référence, mais passent par le Planner.

### 8.8 Fill

- un clic sur un voxel source ;
- flood déterministe sur une connectivité définie ;
- preview complet avant commit ;
- Paint/Replace/Remove uniquement en V1 ;
- aucune traversée hors sous-modèle ;
- limite souple et dure avant allocation.

### 8.9 Surface

Surface vise une région exposée non nécessairement coplanaire. Sa définition
topologique est plus large que Face ; elle reste future afin de ne pas rendre
Face ambigu.

## 9. Face Tools — première classe

### 9.1 Identification d'une face

Une face de départ est définie par :

- position voxel hit ;
- côté du cube hit ;
- normale entière ;
- sous-modèle ;
- révision document ;
- contexte Scene.

Le résolveur construit un masque 2D de cellules support :

1. la cellule contient un voxel ;
2. son côté orienté par la normale est exposé ;
3. elle est coplanaire avec la face initiale ;
4. elle est reliée par connectivité 4 dans le plan ;
5. la politique de correspondance V1 accepte toute couleur.

Une future politique pourra limiter à la même couleur ou au même matériau.

### 9.2 Contours irréguliers et trous

Le flood conserve exactement le masque de support :

- les contours irréguliers restent irréguliers ;
- les trous sans cellule support restent des trous ;
- aucune enveloppe rectangulaire implicite ;
- ordre déterministe ligne/colonne ;
- aucune triangulation nécessaire.

### 9.3 Épaisseur

L'épaisseur est un entier positif :

- Add : couches vers `+normal` ;
- Remove : couches depuis la surface vers `-normal` ;
- Paint/Replace : couches depuis la surface vers `-normal`.

Les cellules absentes ou déjà identiques deviennent des no-op. Les
chevauchements sont orange, pas bloquants.

### 9.4 Opérations

| Opération visible | Définition V1 | Statut |
|---|---|---|
| Face Add | Face + Action Add + épaisseur | V1 |
| Face Remove | Face + Action Remove + épaisseur | V1 |
| Face Paint | Face + Action Paint + épaisseur | V1 |
| Face Replace | Face + Action Replace + épaisseur | V1 |
| Face Extrude | alias UX plus direct de Face Add avec réglage interactif de profondeur | V1.1 |
| Face Inset | érosion du masque, anneau et nouveau contour | Futur |
| Face Fill | reconstruction volontaire de trous d'un contour | Futur |

Face Add avec épaisseur fournit déjà la capacité de base d'une extrusion. Une
commande Extrude dédiée n'est pas introduite en V1, car elle demanderait une
interaction push/pull, la gestion des profondeurs négatives et des états de
capture supplémentaires. Inset est repoussé : l'érosion d'un masque irrégulier
avec trous nécessite une politique topologique explicite.

### 9.5 Preview Face

Le preview affiche :

- cellules support discrètes ;
- couches finales ;
- couleurs finales ;
- cellules ignorées ;
- dépassements ;
- normale et épaisseur sous forme d'indicateurs non colorimétriques ;
- statistiques support/affectées/ignorées/hors limites.

Il ne remplace jamais le résultat par un simple cadre.

## 10. Line Tools

### 10.1 Axis Line

Axis Line contraint le point B à un axe X, Y ou Z relatif au point A. En mode
Auto, l'axe dominant est choisi avec un ordre de départage stable X, Y, Z.

### 10.2 Free 3D Line

Free 3D Line utilise une supercover 3D entière :

- aucune interpolation flottante comme source de cellules ;
- progression rationnelle ;
- toutes les cellules traversées sont émises ;
- égalités départagées dans l'ordre X, Y, Z ;
- doublons supprimés ;
- A et B inclus ;
- résultat identique sur Windows et toute plateforme future.

L'algorithme existant de `VoxelLineService` doit être audité contre ce contrat,
puis conservé ou remplacé dans le résolveur sans maintenir deux variantes.

### 10.3 Épaisseur et Brush

La trajectoire produit des centres. La Brush active est appliquée à chaque
centre. L'union des empreintes est dédupliquée par position, puis triée dans un
ordre canonique. Une taille 1 produit exactement la ligne rasterisée.

### 10.4 Preview et transaction

Entre A et le curseur, chaque nouveau point B produit ou récupère un plan. Le
second clic applique toute la ligne en une seule opération Undo, quelle que
soit son épaisseur.

### 10.5 Polyline et Chain

- Polyline conserve plusieurs segments et valide l'ensemble à Enter ;
- Chain répète un motif ou une Brush orientable le long du chemin.

Elles restent futures pour ne pas imposer prématurément une sémantique de
jonction et d'orientation.

## 11. Brush Shapes

### 11.1 Convention de coordonnées

Pour une dimension entière `N` :

```text
minimumOffset = -((N - 1) / 2)
maximumOffset = N / 2
```

Cette convention correspond au moteur actuel :

- taille impaire : centre sur une cellule ;
- taille paire : centre entre cellules avec biais d'ancrage positif
  documenté ;
- taille 1 : une seule cellule ;
- aucun flottant nécessaire pour la liste finale.

Les calculs intermédiaires de centre utilisent si nécessaire des demi-unités
entières, jamais des floats non déterministes.

### 11.2 Cube

Cube contient toutes les cellules du produit cartésien des offsets sur les
axes actifs.

- Volume3D : `N³` cellules ;
- Surface2D : `N²` cellules dans le plan orienté ;
- largeur/hauteur/profondeur distinctes en options avancées futures ;
- orientation sans effet sur un cube isotrope de dimensions égales.

### 11.3 Sphere

Sphere utilise le centre en demi-cellules. Pour une taille `N`, une cellule
d'offset `(x,y,z)` est incluse si :

```text
dx = 2*x - evenCenterOffset
dy = 2*y - evenCenterOffset
dz = 2*z - evenCenterOffset
dx² + dy² + dz² <= N²
```

`evenCenterOffset` vaut 1 pour une taille paire, 0 sinon. La version Surface2D
emploie le même critère sur deux axes.

### 11.4 Cylinder

Cylinder V1 utilise :

- un axe X, Y ou Z ;
- une longueur entière ;
- une section circulaire suivant la convention Sphere 2D ;
- un pivot central ou de base explicite ;
- taille 1 = une cellule ;
- aucune rotation libre.

Pour le mode Brush simple, largeur = profondeur = taille et hauteur = taille.
Les dimensions séparées restent dans les options avancées.

### 11.5 Diamond

Diamond futur utilise une distance de Manhattan dans l'espace en demi-cellules.
La règle exacte devra être figée avant implémentation pour les tailles paires.

### 11.6 Cone

Cone futur utilise des sections circulaires discrètes dont le rayon varie par
couche selon une formule rationnelle. Aucun échantillonnage flottant ne sera
accepté comme vérité.

### 11.7 Custom Brush et Stamp Brush

- Custom Brush référence un asset de réglages ou un masque local.
- Stamp Brush référence un contenu voxel immuable de Forge Library.

Les deux utilisent le Planner, mais ne font pas du Smart Tool un navigateur.
La sélection de l'asset reste dans Reuse/Forge Library.

## 12. Paramètres communs et contextuels

### 12.1 Paramètres communs

- Action ;
- Mode ;
- palette/couleur active ;
- snap ;
- overwrite policy ;
- preview opacity ;
- profil actif.

### 12.2 Paramètres Brush

- Shape ;
- Size ;
- Width ;
- Height ;
- Depth ;
- Orientation ;
- Pivot ;
- Spacing ;
- Dimension 2D/3D.

### 12.3 Paramètres de mode

| Mode | Paramètres propres |
|---|---|
| Face | connectivité, épaisseur, correspondance de surface |
| Line | Axis/Free, épaisseur, snap axe |
| Rectangle | contour/rempli, plan, épaisseur |
| Box | plein/coque, dimensions |
| Fill | connectivité, limite, correspondance |
| Replace | index source |

### 12.4 Présentation

Toujours visibles :

- Action ;
- Mode ;
- Shape si pertinente ;
- Size ou Thickness principal ;
- couleur active ;
- état du preview.

Repliés dans Advanced :

- dimensions séparées ;
- orientation forcée ;
- spacing ;
- pivot ;
- politique d'overwrite ;
- source Replace ;
- limites et confirmation.

### 12.5 Redimensionnement rapide

Le contrat d'input prévoit :

```text
Ctrl + molette
    ↓
AdjustPrimarySize(delta)
```

La commande modifie le paramètre principal du mode :

- Brush : Size ;
- Face : Thickness ;
- Line : Brush Size ;
- Rectangle : Thickness si actif.

Le binding reste configurable et ne doit pas être codé directement dans le
domaine.

## 13. Brush Profiles

### 13.1 État local inspecté

Les changements Brush Profiles non commités implémentent déjà :

- format JSON versionné V1 ;
- UUID ;
- Save New ;
- Load ;
- Overwrite explicite ;
- Rename ;
- Delete ;
- favoris ;
- récents ;
- recherche et tri ;
- validation stricte ;
- tolérance des champs inconnus ;
- sérialisation de Pencil, Cube/Sphere, Add/Erase/Paint, taille, dimension,
  orientation et opacité ;
- tests CRUD, réouverture, tri, récents, compatibilité, Unicode et validation.

Maturité estimée : **prototype fonctionnel avancé**, mais pas encore contrat
officiel VF-0300.

Écarts :

- stockage uniquement au niveau projet ;
- pas de profils utilisateur globaux ni profils usine ;
- pas de duplication ;
- pas d'identifiant de profil actif dans la session ;
- palette absente ;
- modes Face/Line/Rectangle/Fill absents ;
- options par mode absentes ;
- UI CRUD directement intégrée au Smart Tool ;
- format couplé aux enums prototypes ;
- pas de migration explicite au-delà du rejet de version ;
- pas de backend catalogue partagé avec Forge Library.

### 13.2 Données mémorisées

Un profil peut mémoriser :

- UUID et version ;
- nom, favori, catégorie et ordre utilisateur ;
- Action ;
- Mode ;
- Brush Shape ;
- taille et dimensions ;
- épaisseur ;
- orientation ;
- pivot de Brush ;
- spacing ;
- snap ;
- overwrite policy ;
- palette policy ;
- couleur ou référence palette portable facultative ;
- options propres au mode ;
- opacité de preview si considérée comme préférence de création ;
- versions des réglages et algorithmes.

La palette ne doit pas être un index brut non portable sans contexte. Le profil
stocke soit une couleur RGBA, soit une référence stable de palette, soit
« utiliser la couleur active ».

### 13.3 Données interdites

Un profil ne mémorise jamais :

- document actif ;
- chemin absolu ;
- sous-modèle courant ;
- positions du curseur ;
- hit ou face courante ;
- voxels résolus ;
- plan ou preview courant ;
- sélection ;
- caméra ;
- pile Undo/Redo ;
- état de drag ;
- révision document ;
- erreur temporaire.

### 13.4 Scopes

- profils usine : lecture seule, versionnés avec l'application ;
- profils utilisateur : disponibles dans tous les projets ;
- profils projet : partageables avec le projet ;
- profil actif : UUID + scope dans la session, avec fallback sûr.

### 13.5 Intégration UI et conflit documentaire

VF-0250 décide que la gestion des Brushes appartient à
`Forge Library/Brushes` et qu'aucun navigateur de Brush Profile ne doit
surcharger le Smart Tool. La mission VF-0300 demande néanmoins que
« Brush Profiles » apparaisse dans le panneau contextuel.

Proposition de conciliation :

- le catalogue, la recherche, le CRUD, les favoris et les collections restent
  exclusivement dans Forge Library/Brushes ;
- le panneau Smart Tool montre seulement un sélecteur compact du profil actif,
  les favoris récents et une commande « Open Brushes » ;
- aucun formulaire de gestion complet n'est intégré dans CREATE ;
- appliquer un profil copie ses réglages dans `SmartToolSettings` ; le profil
  n'est pas une source d'état parallèle.

Cette proposition nécessite une validation Tony explicite, car elle précise la
portée de la décision VF-0250.

### 13.6 Migration du prototype

Le service actuel doit être préservé jusqu'à une migration testée :

1. définir le schéma officiel ;
2. écrire un adaptateur du format V1 prototype ;
3. conserver les UUID ;
4. convertir les champs reconnus ;
5. signaler les champs devenus incompatibles ;
6. écrire atomiquement le nouveau format ;
7. ne supprimer l'ancien fichier qu'après validation.

## 14. Preview Engine

### 14.1 Règle centrale

> Le preview ne doit jamais mentir.

Le plan contient les valeurs avant/après. Le PreviewAdapter ne décide rien ; il
convertit chaque cellule en donnée d'affichage.

### 14.2 États

| État | Couleur indicative | Indicateur non colorimétrique |
|---|---|---|
| ajout/modification valide | vert | cellule pleine/plus |
| chevauchement ou no-op autorisé | orange | hachure/contour |
| suppression | rouge désaturé | symbole moins/contour évidé |
| erreur dure | rouge | croix + message |
| hors zone guide mais autorisé | orange | bord pointillé |
| hors limites techniques | rouge | cellule barrée |

La couleur n'est jamais le seul indicateur.

### 14.3 Contrat commun

Le contrat générique actuel `VoxelPreviewData` doit être étendu ou adapté pour
représenter :

- valeur avant ;
- valeur après ;
- type de mutation ;
- no-op ;
- diagnostic ;
- état validable.

Il ne faut pas créer un troisième preview parallèle. La migration converge vers
un contrat générique consommable par Smart Tool, Stamps et futurs Build Tools.

### 14.4 Exactitude des grosses opérations

Une boîte agrégée ne suffit pas à autoriser un commit. Pour une opération trop
grande :

1. une indication de chargement peut apparaître ;
2. le plan exact se construit de manière annulable ;
3. `CanCommit` reste faux tant que le plan n'est pas complet ;
4. le renderer reçoit ensuite toutes les cellules exactes, éventuellement via
   un buffer compact ou des lots ;
5. aucune approximation n'est présentée comme résultat final.

### 14.5 Cache

Le plan est recalculé seulement si sa clé change. Un mouvement souris qui reste
dans la même cellule et conserve le même hit ne déclenche rien.

## 15. Collisions, limites et Construction Box

### 15.1 Chevauchements

Politique par défaut :

- non bloquante ;
- Add ignore les cellules existantes ;
- Paint/Replace/Remove appliquent leur sémantique ;
- no-op et overlaps sont orange ;
- le plan reste validable s'il contient au moins un changement et aucune
  erreur dure.

Politiques restrictives futures :

- Reject ;
- SkipOccupied ;
- Overwrite explicite lorsque pertinent.

### 15.2 Construction Box

Le Smart Tool ne possède pas de Construction Box. Scene fournit :

- grille active ;
- Workplane actif ;
- origine ;
- axes ;
- snap ;
- sous-modèle cible ;
- dimensions de référence ;
- diagnostic de zone.

La Construction Box est un guide :

- création à l'extérieur autorisée par défaut ;
- intersection autorisée ;
- diagnostic orange ;
- politique stricte optionnelle.

### 15.3 Limites techniques actuelles

Le document actuel possède des dimensions fixes. Tant que son extension
atomique n'existe pas, une cellule hors dimensions reste une erreur technique
rouge. Cette restriction vient du document, pas de la Construction Box.

Le contrat prépare une future politique :

- RejectOutsideDocument ;
- ExpandDocument ;
- ClipExplicit.

`ClipExplicit` ne doit jamais être implicite, car un preview partiel pourrait
surprendre l'utilisateur.

## 16. Transactions et Undo/Redo

### 16.1 Règle par geste

| Geste | Transaction |
|---|---|
| clic Single Voxel | une |
| clic Brush | une |
| drag Brush complet | une |
| Face validée | une |
| Line A→B | une |
| Rectangle A→B | une |
| Fill | une |
| Pick | aucune |

Un événement souris n'est pas automatiquement une transaction. La session
définit les limites du geste.

### 16.2 Construction de l'opération

Le CommitAdapter transforme `ResolvedCells` en un seul `VoxelEditOperation` :

- label localisable ;
- changements avant/après ;
- changement palette éventuel ;
- transition Selection seulement si le contrat le demande ;
- statistiques facultatives hors historique.

Il appelle `VoxelEditHistory::Execute`. Il n'écrit jamais directement dans le
document, la grille ou le mesh.

### 16.3 Drag continu

V1 :

- échantillonnage spatial ;
- déduplication par position ;
- conservation de la première valeur `Before` ;
- dernière valeur `After` déterministe ;
- plan cumulatif du geste ;
- commit au relâchement ;
- un Undo pour tout le geste.

Pour éviter une perte excessive en cas d'interruption, une évolution pourra
utiliser des sous-lots techniques tout en conservant une opération historique
logique unique. Cette optimisation exige une extension explicite de
`VoxelEditHistory`.

### 16.4 Coûts connus

STAMP-16 a montré que snapshots complets et rebuild mesh dominent les grosses
opérations. VF-0300 n'autorise pas une reconstruction par cellule ni par
événement souris :

- une validation ;
- une mutation composite ;
- un rebuild ;
- une entrée d'historique.

## 17. Interface utilisateur

### 17.1 Toolbar

Toujours trois boutons :

1. Smart Tool ;
2. Selection ;
3. Transform.

Les anciens boutons Erase, Paint, Fill, Box, Line et Sphere ne sont pas
conservés comme outils principaux.

### 17.2 Panneau contextuel

Ordre recommandé :

```text
SMART TOOL
Action
Mode
Brush
Size / Thickness
Palette
Preview status + statistics
Advanced (collapsed)
Active Profile (compact)
```

Le panneau n'affiche que les contrôles compatibles. Exemple : Pick masque Brush
et Size ; Face affiche Thickness ; Line affiche Axis/Free.

### 17.3 Panneau étroit

Largeur indicative `< 260 px` :

- Action et Mode en combos illustrées ;
- Shape sous forme de 2–3 boutons compacts ;
- Size pleine largeur ;
- palette comme swatch + nom ;
- Advanced replié ;
- profil actif sur une ligne ;
- aucun groupe horizontal coupé.

### 17.4 Largeur normale

Largeur `260–380 px` :

- actions principales en segments ;
- modes fréquents en grille 2 colonnes ;
- Brush et Size côte à côte si lisibles ;
- statistiques compactes ;
- fonctions futures dans « More ».

### 17.5 Panneau large

Largeur `> 380 px` :

- actions et modes en lignes illustrées ;
- paramètres Brush en grille 2 colonnes ;
- statistiques détaillées ;
- aucune augmentation du nombre fonctionnel de commandes visibles.

### 17.6 Icônes

Direction officielle :

- illustrées en couleur ;
- isométriques ou en léger relief voxel ;
- silhouette distincte ;
- lisibles en 20–32 px ;
- état actif renforcé par cadre, relief et label ;
- jamais distinguées uniquement par la teinte ;
- pas de direction monochrome comme résultat final.

Icônes pilotes prioritaires :

- Add ;
- Remove ;
- Paint ;
- Replace ;
- Voxel ;
- Brush ;
- Face ;
- Line ;
- Rectangle ;
- Fill ;
- Cube ;
- Sphere ;
- Cylinder ;
- Size ;
- Palette ;
- Brush Profile.

VF-0290 devra fixer couleurs, espacements, états et accessibilité. VF-0291
devra fixer grille, angles, export, nommage et tests visuels.

## 18. Raccourcis

Les raccourcis sont des commandes configurables, pas des tests de touches dans
le Planner.

Stratégie :

- une commande pour activer Smart Tool ;
- palette de commandes ou menu radial/contextuel pour Action et Mode ;
- raccourcis mémorisables pour les fonctions fréquentes ;
- Ctrl + molette pour le paramètre principal ;
- maintien temporaire possible pour Pick ;
- Esc annule d'abord l'interaction, puis désactive selon le contexte ;
- possibilité future de remappage.

Avant attribution définitive, chaque commande doit être comparée aux bindings
Selection, Transform, caméra et menus. Les raccourcis historiques peuvent
devenir des alias de migration :

- Pencil → Smart Tool + Single/Brush + Add ;
- Eraser → Smart Tool + Single/Brush + Remove ;
- Paint → Smart Tool + Single/Brush + Paint ;
- Line → Smart Tool + Line ;
- Box → Smart Tool + Box.

Ils ne réintroduisent pas les anciens noms dans la Toolbar.

## 19. Accessibilité

Exigences :

- label accessible pour chaque icône ;
- tooltip avec action, mode et raccourci ;
- silhouette distincte ;
- actif/hover/désactivé visibles sans couleur ;
- contraste compatible fond sombre ;
- motif pour overlaps et erreurs ;
- taille DPI issue du Design System ;
- navigation clavier du panneau ;
- ordre de tabulation stable ;
- messages d'erreur explicites ;
- support d'une préférence de réduction d'animation ;
- aucune information portée uniquement par rouge/vert.

## 20. Performance et budgets V1

### 20.1 Risques

- grosses brushes ;
- face très étendue ;
- ligne épaisse ;
- Fill sur grand volume ;
- drag continu ;
- copies de plans ;
- snapshots Undo ;
- rebuild mesh ;
- upload renderer ;
- recalcul à chaque pixel souris.

### 20.2 Budgets initiaux

Ces valeurs sont des politiques modifiables après benchmark :

| Élément | Seuil V1 |
|---|---:|
| taille primaire Brush | 1–16, conforme au moteur actuel |
| preview détaillé interactif cible | 32 768 cellules |
| avertissement opération | 65 536 changements |
| limite dure plan V1 | 262 144 cellules résolues |
| mémoire temporaire cible | 64 Mio |
| recalcul si cellule/hit inchangé | 0 |
| rebuild par commit | 1 |
| entrée Undo par geste | 1 |

Une opération dépassant le seuil interactif peut être planifiée de manière
asynchrone/annulable plus tard. Elle ne devient validable qu'une fois le
preview exact disponible.

### 20.3 Mesures obligatoires

- temps Planner total et par résolveur ;
- allocations ;
- cellules candidates, uniques, affectées et ignorées ;
- mémoire du plan ;
- temps PreviewAdapter ;
- temps CommitAdapter ;
- temps transaction ;
- temps rebuild ;
- temps upload ;
- mémoire Undo ;
- taux de réutilisation du cache.

### 20.4 Optimisations autorisées

- `reserve` fondé sur estimation sûre ;
- réutilisation de buffers appartenant à la session ;
- déduplication compacte ;
- déplacement d'objets ;
- cache par clé complète ;
- itération sparse ;
- plan partagé const.

Toute optimisation doit être mesurée et ne change pas la sémantique.

## 21. Relations avec les autres piliers

### 21.1 Scene

Scene possède :

- sous-modèle cible ;
- origine et axes ;
- grille ;
- Workplane ;
- Construction Box ;
- politiques de zone.

Le Smart Tool consomme un snapshot de contexte.

### 21.2 Selection

Selection fournit :

- région ou masque facultatif ;
- bounds ;
- sélection persistante.

Le Smart Tool peut respecter un masque explicite, sans devenir le propriétaire
de Selection.

### 21.3 Transform

Transform modifie une sélection. Il ne tourne pas la Brush active par effet de
bord. Une orientation de Brush est un réglage Smart Tool séparé.

### 21.4 Reuse / Stamps

Stamps fournit des contenus voxel réutilisables. Un futur Stamp Brush peut
fournir une empreinte au Planner. Le navigateur reste Forge Library.

### 21.5 Build Tools

Les Build Tools peuvent réutiliser :

- résolveurs géométriques ;
- plan ;
- preview ;
- commit atomique.

Ils ne deviennent pas une longue liste de modes cachés du Smart Tool si leur
workflow nécessite un outil dédié.

### 21.6 Forge Academy

Academy peut :

- activer Smart Tool ;
- charger un profil ;
- mettre en évidence une option ;
- vérifier une étape.

Elle utilise les mêmes commandes et ne crée pas d'interface parallèle.

### 21.7 IA

L'IA peut :

- proposer une Action/Mode/Brush ;
- proposer un profil ;
- expliquer un diagnostic ;
- préparer un plan à prévisualiser.

Elle ne valide jamais le commit sans action utilisateur explicite.

## 22. Périmètre recommandé

### 22.1 V1 obligatoire

Actions :

- Add ;
- Remove ;
- Paint ;
- Replace ;
- Pick.

Modes :

- Single Voxel ;
- Brush ;
- Face ;
- Line ;
- Rectangle ;
- Fill.

Shapes :

- Cube ;
- Sphere ;
- Cylinder.

Fondations :

- Session ;
- Planner ;
- plan immuable ;
- PreviewAdapter générique ;
- CommitAdapter ;
- cache complet ;
- SceneContext ;
- palette ;
- preview exact ;
- Undo/Redo ;
- limites ;
- raccourci de taille ;
- intégration de profils compatible avec la décision UI finale.

### 22.2 V1.1

- Plane ;
- Box unifié ;
- Circle ;
- Sphere mode ;
- Cylinder mode ;
- Extrude interactif dédié ;
- drag continu avancé ;
- dimensions séparées ;
- profils utilisateur globaux et profils usine ;
- UI responsive finalisée ;
- commandes personnalisables.

### 22.3 Futur

- Inset ;
- Face Fill ;
- Surface ;
- Cone ;
- Diamond ;
- Polyline ;
- Chain ;
- Shell ;
- Custom Brush ;
- Stamp Brush ;
- Noise ;
- Smooth ;
- Material Paint ;
- Pattern ;
- Scatter ;
- falloff ;
- symétrie ;
- assistance IA.

## 23. Stratégie de tests

### 23.1 Domaine

- validation exhaustive des enums et settings ;
- sérialisation/version des settings et profils ;
- tailles paires/impaires ;
- taille 1 ;
- overflow ;
- déterminisme inter-exécutions ;
- absence de doublons ;
- ordre canonique.

### 23.2 Actions

Pour chaque action :

- vide ;
- occupé même couleur ;
- occupé couleur différente ;
- palette invalide ;
- no-op ;
- Undo ;
- Redo ;
- révision unique ;
- rollback.

### 23.3 Modes

- Single hit/adjacent/Workplane ;
- Brush 2D/3D/orientations ;
- Face irrégulière/trou/épaisseur/normale ;
- Line axes/diagonales/symétrie/inversion A-B ;
- Rectangle plans/contour/rempli ;
- Fill connectivité/limites/grosse région ;
- interactions cancel/document switch.

### 23.4 Preview

- plan == preview ;
- plan == commit ;
- couleurs et états ;
- overlap orange validable ;
- erreur rouge non validable ;
- changement de révision invalide le commit ;
- cache réutilisé si clé identique ;
- aucun recalcul au clic ;
- gros plan non validable avant preview exact.

### 23.5 UI et accessibilité

- Toolbar exactement trois boutons ;
- panneau contextuel ;
- modes incompatibles cachés/désactivés ;
- panneaux étroit/normal/large ;
- labels/tooltips ;
- navigation clavier ;
- DPI ;
- test visuel d'icônes ;
- aucune collision d'identifiants ImGui.

### 23.6 Performance

Benchmarks séparés de CTest :

- 1, 64, 512, 4 096, 32 768, 65 536 et 262 144 cellules ;
- Face large ;
- Line épaisse ;
- Fill dense/sparse ;
- drag ;
- cache hit/miss ;
- plan/preview/commit/rebuild/Undo/Redo.

## 24. Lots d'implémentation préliminaires

### SMART-01 — Domain types and compatibility map

- types canoniques Action/Mode/Brush/Settings ;
- validation ;
- mapping temporaire depuis les enums actuels ;
- aucun comportement nouveau.

### SMART-02 — Scene context and interaction session

- `SmartToolSceneContext` ;
- `SmartToolSession` ;
- phases de geste ;
- invalidation document/génération/révision.

### SMART-03 — Immutable plan and Planner shell

- plan immuable ;
- clé de cache ;
- diagnostics ;
- limites ;
- interfaces internes des résolveurs.

### SMART-04 — Generic preview and commit adapters

- convergence des contrats de preview ;
- adaptation exacte plan → preview ;
- plan → `VoxelEditOperation` ;
- test Preview == Commit.

### SMART-05 — Single Voxel and Brush migration

- Add/Remove/Paint ;
- Cube/Sphere ;
- retrait des doubles résolutions ;
- aliases de raccourcis historiques.

### SMART-06 — Replace, Pick and Cylinder

- source Replace ;
- Pick temporaire ;
- Cylinder déterministe ;
- profils de réglages étendus.

### SMART-07 — Face foundation

- identification ;
- masque coplanaire ;
- trous/contours ;
- Add/Remove/Paint/Replace ;
- épaisseur.

### SMART-08 — Line

- Axis/Free 3D ;
- supercover ;
- Brush le long du chemin ;
- interaction deux points.

### SMART-09 — Rectangle

- plan orienté ;
- contour/rempli ;
- épaisseur ;
- préparation Plane V1.1.

### SMART-10 — Fill

- région connectée ;
- Paint/Replace/Remove ;
- budgets et annulation.

### SMART-11 — Brush Profiles migration and library integration

- schéma officiel ;
- migration du prototype ;
- scopes ;
- profil actif ;
- intégration Forge Library/Brushes ;
- raccourci compact selon décision Tony.

### SMART-12 — Input, UX, accessibility and icon pilots

- panneau responsive ;
- commandes ;
- Ctrl+molette ;
- icônes pilotes VF-0291 ;
- accessibilité.

### SMART-13 — Performance hardening

- benchmarks ;
- drag ;
- mémoire Undo ;
- rebuild ;
- seuils.

### SMART-14 — Architecture and product review

- audit dépendances ;
- suppression des anciens outils parallèles ;
- validation Tony ;
- documentation ;
- release gate V1.

Ordre :

```text
01 → 02 → 03 → 04 → 05
                   ├→ 06
                   ├→ 07
                   ├→ 08
                   ├→ 09
                   └→ 10
06..10 → 11 → 12 → 13 → 14
```

## 25. Risques et dette connue

| Risque | Niveau | Réponse |
|---|---|---|
| migration des outils historiques | fort | aliases temporaires, tests de parité, suppression tardive |
| preview parallèle | fort | convergence SMART-04 avant nouveaux modes |
| gros plans et snapshots | fort | budgets, benchmarks, un rebuild par geste |
| Face topologique | moyen/fort | masque coplanaire V1 strict, Surface séparé |
| drag continu | moyen/fort | geste atomique et échantillonnage spatial |
| Brush Profiles non commités | moyen | préserver, figer schéma officiel puis migrer |
| conflit UI Profiles/VF-0250 | moyen | décision Tony avant SMART-11 |
| document à dimensions fixes | moyen | diagnostic clair, contrat d'expansion futur |
| duplication Shape/Mode | moyen | types canoniques SMART-01 |
| `EditorWorkspace` trop central | moyen | Controller applicatif progressif |
| raccourcis existants | faible/moyen | registre de commandes configurable |
| rendu exact très volumineux | moyen | buffer compact, commit bloqué avant disponibilité |

## 26. Décisions proposées

| Sujet | Décision VF-0300 |
|---|---|
| Outils principaux | Smart Tool, Selection, Transform uniquement |
| Source de vérité | `SmartToolPlan` immuable |
| Preview/Commit | consomment le même plan, aucun recalcul au clic |
| Fill | mode de région, pas action |
| Face | première classe V1 |
| Extrude | capacité de base via Face Add + Thickness ; UX dédiée V1.1 |
| Inset | futur |
| Line | Axis et Free 3D en V1 |
| Shapes V1 | Cube, Sphere, Cylinder |
| Overlap | non bloquant par défaut, orange |
| Construction Box | propriété Scene, guide non bloquant |
| Limites document | erreurs techniques tant que l'expansion n'existe pas |
| Undo | une transaction par geste |
| Icônes | illustrées en couleur, silhouette distincte |
| IA | propose, ne valide jamais |

## 27. Questions nécessitant validation Tony

1. **Brush Profiles dans CREATE** — confirmer la conciliation proposée :
   gestion complète dans Forge Library/Brushes, mais sélecteur compact du
   profil actif dans le panneau Smart Tool. L'alternative stricte VF-0250 est
   de n'afficher aucun profil dans CREATE.
2. **Face Add et Extrude** — confirmer que Face Add + Thickness couvre la V1
   et que l'interaction push/pull « Extrude » attend V1.1.
3. **Limites du document** — confirmer que la V1 du Smart Tool refuse les
   cellules hors dimensions techniques, tout en gardant la Construction Box
   non bloquante, jusqu'à une mission d'expansion atomique du document.
4. **Drag Brush V1** — confirmer qu'un geste press→drag→release produit une
   seule transaction, plutôt qu'une transaction par échantillon.
5. **Add sur cellule existante** — confirmer la politique proposée : no-op
   orange, et non remplacement implicite. Replace reste l'action explicite.
6. **Budget exact du preview** — valider les seuils initiaux après benchmark,
   notamment 32 768 cellules interactives et 262 144 cellules dures.

Les cinq premières décisions n'empêchent pas SMART-01 à SMART-04 si les types
restent configurables. Elles doivent être closes avant les lots fonctionnels
concernés.

## 28. Critères d'acceptation de l'architecture

VF-0300 est prête à devenir la référence après validation si :

- Face et Line sont des modes de première classe ;
- la Toolbar reste à trois outils ;
- Actions, Modes et Shapes sont orthogonaux ;
- Fill n'est pas dupliqué ;
- Preview et Commit partagent le plan immuable ;
- aucun calcul métier n'est dans le renderer ;
- Construction Box et Workplane restent des contrats Scene ;
- overlaps non bloquants et erreurs dures sont distingués ;
- Undo/Redo est atomique par geste ;
- les Brush Profiles ont une migration et des scopes clairs ;
- l'UI responsive limite les boutons ;
- les icônes en couleur et l'accessibilité sont contractuelles ;
- le périmètre V1 est réalisable par lots indépendants ;
- aucune dépendance circulaire n'est introduite.

## 29. Conclusion

Le dépôt possède déjà les briques essentielles : moteur de Brush,
transactions atomiques, historique, picking, Workplane, palette, Ghost Preview
et plusieurs outils géométriques. La prochaine étape ne doit pas ajouter un
nouveau mode directement dans `EditorWorkspace`. Elle doit d'abord consolider
les types, la session et le plan immuable.

Le modèle :

```text
Action × Mode × Brush
          ↓
     Planner unique
          ↓
    Plan immuable exact
       ↙         ↘
   Preview       Commit
```

permet d'ajouter Face, Line, Rectangle, Fill et les futures formes sans
dupliquer la logique ni mentir à l'utilisateur. Il conserve la simplicité
visible de VoxelForge Studio tout en rendant son moteur de création extensible.

**Recommandation :** valider les six questions du chapitre 27, puis produire un
plan d'implémentation détaillé commençant par SMART-01. Aucun développement
fonctionnel ne devrait précéder cette validation.
