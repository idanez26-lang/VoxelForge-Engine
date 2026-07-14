# VF-0230 — Viewport 3D Prototype

## Objectif

Fournir immédiatement une première expérience 3D interactive dans le panneau
Viewport, avant l'intégration du renderer GPU définitif.

## Fonctionnalités

- projection perspective ;
- grille 3D ;
- axes X, Y et Z ;
- cube filaire ;
- caméra orbitale/libre ;
- déplacement WASD ;
- élévation Q/E ;
- accélération Shift ;
- zoom molette.

## Architecture temporaire

Le rendu est effectué avec `ImDrawList`.

Ce choix permet de valider :

- l'UX de la caméra ;
- la disposition du Viewport ;
- le lien entre Scene et Viewport ;
- les conventions d'axes.

Il ne remplace pas le futur renderer GPU.

## Étape suivante

Le futur module Renderer reprendra la même caméra et la même scène, mais dessinera
dans une texture GPU affichée dans ImGui.
