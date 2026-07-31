# Installation — VF-0170 Layer System

1. Fermer Visual Studio.
2. Décompresser ce ZIP dans :
   E:\VoxelForge-Engine
3. Autoriser la fusion des dossiers.
4. Autoriser le remplacement de :
   - engine/Core/CMakeLists.txt
   - engine/Core/include/VoxelForge/Core/Application.h
   - engine/Core/src/Application.cpp
5. Rouvrir Visual Studio.
6. Attendre la régénération CMake.
7. Compiler avec Ctrl + Shift + B.
8. Lancer VoxelForgeEditor.exe.

Résultat attendu :
- compilation réussie ;
- création du DemoProject ;
- démarrage et arrêt avec le code 0.

Le test fourni n'est pas ajouté automatiquement au build principal.
