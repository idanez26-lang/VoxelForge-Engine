# VF-0100 — Modèle métier principal

## Concepts fondamentaux

### Workspace
Environnement personnel : interface, raccourcis, plugins, bibliothèques et préférences.

### Project
Conteneur complet d’une création. Il contient scènes, assets, historique, paramètres, sessions créatives et exports.

### Creative Session
Historique d’une période de création : conversation, prompts, références, variantes, décisions et jalons.

### Scene
Espace organisé contenant instances d’assets, environnement, caméras et lumières.

### Asset
Ressource voxel réutilisable : objet, bâtiment, personnage, végétation, véhicule ou autre création.

### Asset Instance
Utilisation d’un Asset dans une Scene avec position, rotation, échelle et modifications locales.

### Blueprint
Description logique et procédurale permettant de construire ou reconstruire un Asset.

### Asset DNA
Identité d’un Asset : catégorie, style, époque, matériaux, palette, usure, niveau de détail et seed.

### Builder
Orchestrateur spécialisé dans une famille de créations.

### Capability
Capacité élémentaire réutilisable : créer un mur, un toit, une fenêtre, un tronc, appliquer une palette, optimiser des voxels.

### Build Graph
Plan exécutable des étapes de construction et de leurs dépendances.

### Generation Request
Demande issue d’un texte, d’une image, d’une sélection ou d’une modification manuelle.

### Generation Result
Résultat traçable avec seed, paramètres, modules utilisés, avertissements et rapport qualité.

### Variant
Version alternative d’un Asset ou Blueprint.

### Style
Règles artistiques chargées sous forme de données.

### Palette
Ensemble de couleurs et règles Pixel Art.

### Material
Propriétés génériques d’une surface voxel, indépendantes d’un moteur cible.

### Knowledge Pack
Connaissances versionnées : architecture, botanique, véhicules, matériaux, urbanisme, couleur, voxel art.

### Export Profile
Traduction vers une cible : VOX, OBJ, GLTF, Teardown, Blender, Unity, etc.

### Plugin
Extension pouvant fournir Builders, Capabilities, Styles, Knowledge Packs, exporteurs ou outils.

### History Entry
Action enregistrée avec état avant, état après, auteur, date et raison.

### Creative Timeline
Historique artistique avec branches, comparaisons et restaurations.

## Relations principales

```text
Workspace
└── Projects
    ├── Creative Sessions
    ├── Scenes
    │   └── Asset Instances
    ├── Assets
    │   ├── Blueprints
    │   └── Asset DNA
    ├── Styles / Palettes / Materials
    ├── Generations / Variants
    └── History / Exports
```

## Invariants

- contrôle final de l’artiste ;
- non-destruction ;
- reproductibilité ;
- indépendance de l’IA ;
- universalité ;
- traçabilité ;
- extensibilité ;
- transparence ;
- éditabilité ;
- portabilité.
