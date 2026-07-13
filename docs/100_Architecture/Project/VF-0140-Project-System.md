# VF-0140 — Project System

**Version :** 0.0.3  
**Statut :** Première implémentation

## Objectif

Créer et gérer un projet VoxelForge actif.

## Composants

- `Project` : identité, nom et emplacement.
- `ProjectManager` : création, activation et fermeture.
- `project.json` : métadonnées minimales du projet.

## Limites actuelles

- pas encore de chargement de projet ;
- pas encore de migration de format ;
- pas encore de scène ou d’asset ;
- sérialisation JSON minimale écrite sans bibliothèque externe.

Ces fonctions seront ajoutées progressivement.
