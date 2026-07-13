# VoxelForge Engine — v0.0.3 FIXED

Correction de la dépendance circulaire :

- `VoxelForgeCore` ne dépend plus de `VoxelForgeProject`.
- `VoxelForgeProject` dépend de `VoxelForgeCore`.
- `VoxelForgeEditor` orchestre les deux modules.
- Les chemins d’inclusion ont été vérifiés.
- La compilation CMake a été testée avant livraison.
