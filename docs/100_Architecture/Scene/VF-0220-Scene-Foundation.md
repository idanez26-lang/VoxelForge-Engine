# VF-0220 — Scene Foundation

## Objectif

Créer les premières données réelles de scène utilisées simultanément par :

- la Hierarchy ;
- l'Inspector ;
- le futur Viewport ;
- les futurs systèmes Forge AI.

## Modules

### Scene

Possède les entités et gère leur création, leur recherche et leur destruction.

### Entity

Carte d'identité d'un objet de scène :

- UUID ;
- nom ;
- Transform ;
- Metadata ;
- Forge DNA.

### TransformComponent

- position ;
- rotation ;
- échelle.

### MetadataComponent

- auteur ;
- catégorie ;
- tags.

### ForgeDNAComponent

Première base sémantique destinée aux futurs assistants :

- style ;
- palette ;
- état ;
- usage.

## Interaction éditeur

La sélection d'une entité dans la Hierarchy alimente l'Inspector.
Le nom et le Transform peuvent être modifiés directement.

## Limites volontaires

Cette version n'est pas encore un ECS complet.
Elle privilégie une base petite, stable et compréhensible avant d'ajouter :

- relations parent/enfant ;
- composants dynamiques ;
- sérialisation ;
- Undo/Redo ;
- prefabs.
