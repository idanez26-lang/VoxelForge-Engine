# ADR-0001 — Architecture modulaire

**Statut :** Accepté

## Contexte

VoxelForge doit accueillir de nouveaux générateurs, styles, IA et exporteurs pendant plusieurs années.

## Décision

Chaque système majeur est un module indépendant avec contrat public, version et dépendances déclarées.

## Conséquences positives

- remplacement plus facile ;
- tests isolés ;
- développement parallèle ;
- plugins plus sûrs ;
- meilleure maintenance.

## Risques

- architecture initiale plus exigeante ;
- besoin de contrats précis ;
- gestion du versionnage.
