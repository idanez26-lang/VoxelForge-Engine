# Changelog — VF-0190 Input System

## Ajouté
- API publique Input ;
- KeyCode indépendant de SDL ;
- MouseCode indépendant de SDL ;
- interrogation de l'état du clavier ;
- interrogation des boutons de souris ;
- position et delta de souris ;
- événements clavier SDL3 ;
- événements déplacement, molette et boutons souris SDL3.

## Modifié
- KeyboardEvent utilise désormais KeyCode ;
- MouseEvent utilise désormais MouseCode ;
- SDLWindow traduit les événements SDL3 vers les événements VoxelForge ;
- CMake compile désormais Input.cpp.
