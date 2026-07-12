# VF-0110 — Architecture globale

```text
User Experience
├── Conversation
├── Viewport
├── Inspector
├── Timeline
├── Library
└── Expert Mode

Creative Intelligence
├── Intent Engine
├── Context Memory
├── Question Manager
├── Creative Planner
└── AI Provider Adapters

Creation Engine
├── Builder Registry
├── Capability Registry
├── Build Graph
├── Voxel Scene
└── Optimization

Artistic Systems
├── Style Engine
├── Palette Engine
├── Material Engine
├── Art Director
└── Quality Validator

Engine Services
├── Project System
├── Asset Library
├── Rendering
├── Export
└── Plugins

Core
├── Tasks
├── Events
├── Resources
├── Logging
├── Configuration
└── File System
```

## Règle de dépendance

Les couches supérieures utilisent les services des couches inférieures. Le Core ne dépend jamais de l’IA, du rendu, de l’interface ou d’un exporteur particulier.

## Exécution sans interface

Le moteur doit être utilisable :

- depuis l’éditeur VoxelForge ;
- en ligne de commande ;
- via un SDK ;
- depuis un plugin externe ;
- via une API future.
