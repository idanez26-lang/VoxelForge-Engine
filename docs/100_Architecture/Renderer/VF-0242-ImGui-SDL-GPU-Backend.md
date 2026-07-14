# VF-0242 — ImGui SDL GPU Backend

## Objectif

Faire rendre toute l'interface VoxelForge par SDL GPU au lieu de SDL_Renderer.

## Pourquoi cette migration précède le triangle

La fenêtre ne peut pas rester partagée entre deux systèmes de rendu concurrents.
Le GPU doit devenir propriétaire de la fenêtre avant d'ajouter :

- les passes de rendu ;
- les shaders ;
- les textures du Viewport ;
- le premier triangle GPU.

## Nouveau cycle

1. Le Renderer crée le SDL_GPUDevice.
2. SDLWindow crée la fenêtre.
3. Le GPU revendique la fenêtre.
4. Dear ImGui utilise `imgui_impl_sdlgpu3`.
5. Chaque frame acquiert une commande GPU et une texture de swapchain.
6. ImGui prépare ses données avant le render pass.
7. Le render pass affiche l'interface.
8. Le command buffer est soumis au GPU.

## Résultat visible

L'interface doit être identique, mais elle est désormais entièrement rendue par
le backend GPU moderne de SDL3.
