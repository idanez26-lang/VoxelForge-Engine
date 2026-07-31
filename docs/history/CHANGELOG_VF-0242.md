# Changelog — VF-0242 ImGui SDL GPU Backend

## Ajouté
- rendu complet de Dear ImGui avec SDL GPU ;
- revendication de la fenêtre par le GPU ;
- swapchain SDL GPU ;
- command buffer par frame ;
- render pass GPU pour l'interface.

## Supprimé
- backend ImGui SDL_Renderer3 ;
- SDL_Renderer de la fenêtre principale.

## Refactorisé
- le Renderer est désormais initialisé avant la fenêtre ;
- le module Renderer ne dépend plus du Core ;
- le Core utilise le périphérique créé par Renderer.

## Conservé
- Dockspace ;
- Scene ;
- Inspector ;
- caméra ;
- grille et cube filaire temporaires.
