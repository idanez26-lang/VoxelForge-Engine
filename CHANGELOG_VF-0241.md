# Changelog — VF-0241 SDL GPU Device

## Ajouté

- création réelle d'un SDL_GPUDevice ;
- sélection automatique du backend GPU ;
- diagnostic du backend ;
- diagnostic des formats de shaders ;
- accès contrôlé au périphérique ;
- arrêt propre avec attente de fin GPU ;
- affichage du diagnostic dans la Console et la barre d'état.

## Modifié

- Renderer ;
- CMake du module Renderer ;
- EditorLayer.

## Conservé

- Viewport ImGui prototype ;
- caméra ;
- grille ;
- cube filaire ;
- Scene et Inspector.
