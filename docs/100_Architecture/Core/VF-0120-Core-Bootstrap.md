# VF-0120 — Core Bootstrap

**Version :** 0.0.2  
**Statut :** Implémenté

## Objectif

Fournir le premier démarrage compilable de VoxelForge Engine.

## Composants

- `Application` : cycle de vie minimal du moteur.
- `Logger` : journalisation thread-safe vers la console.
- `Version` : version officielle compilée.
- `VoxelForgeCore` : bibliothèque statique du cœur.
- `VoxelForgeEditor` : exécutable minimal.

## Règles

- Le Core ne dépend pas de l’interface, de l’IA ou d’un exporteur.
- L’exécutable dépend du Core, jamais l’inverse.
- Les erreurs non gérées sont interceptées à l’entrée du programme.
- La compilation utilise C++20.
