# Installation VF-0150

1. Fermer Visual Studio.
2. Décompresser ce ZIP dans `E:\VoxelForge-Engine`.
3. Autoriser la fusion des dossiers et le remplacement des six anciens headers.
4. Rouvrir Visual Studio.
5. Compiler avec `Ctrl + Shift + B`.

Aucune modification CMake n'est requise si `engine/Core/include` est déjà exposé.
Le test fourni dans `tests/Core` n'est pas ajouté automatiquement au build principal.
