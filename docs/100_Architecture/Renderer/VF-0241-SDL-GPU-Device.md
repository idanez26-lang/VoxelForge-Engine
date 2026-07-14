# VF-0241 — SDL GPU Device

## Objectif

Créer le premier véritable périphérique GPU de VoxelForge sans encore
remplacer le Viewport prototype.

## Fonctionnement

Le module Renderer :

1. demande à SDL de sélectionner le meilleur backend disponible ;
2. annonce les formats de shaders que VoxelForge pourra fournir ;
3. crée le périphérique GPU ;
4. enregistre le nom du backend sélectionné ;
5. enregistre les formats de shaders supportés ;
6. libère proprement le périphérique à l'arrêt.

## Backends possibles

Selon la machine et le système :

- Direct3D 12 ;
- Vulkan ;
- Metal.

## Pourquoi aucun triangle dans cette étape

La fenêtre actuelle utilise encore SDL_Renderer pour Dear ImGui.
VF-0241 valide d'abord la disponibilité du backend SDL GPU sans revendiquer
la fenêtre ni casser l'interface existante.

L'affichage GPU dans le Viewport sera réalisé ensuite avec une cible de rendu
hors écran, puis intégrée à ImGui.

## Validation

La Console intégrée doit afficher :

- SDL GPU device actif ;
- le backend sélectionné ;
- les formats de shaders disponibles.
