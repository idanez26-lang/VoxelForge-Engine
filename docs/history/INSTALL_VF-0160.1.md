# Installation — VF-0160.1 Correctif Application

1. Fermer Visual Studio.
2. Décompresser ce ZIP dans :
   E:\VoxelForge-Engine
3. Autoriser la fusion des dossiers.
4. Autoriser le remplacement de :
   engine/Core/src/Application.cpp
5. Rouvrir Visual Studio.
6. Compiler avec Ctrl + Shift + B.
7. Lancer VoxelForgeEditor.exe.

Résultat attendu :
- aucune violation d'accès ;
- affichage des messages d'initialisation ;
- arrêt propre après Entrée.

Aucun fichier CMake ne doit être modifié.
