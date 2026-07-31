# Installation — VF-0190 Input System

1. Fermer complètement Visual Studio.
2. Décompresser ce ZIP dans :
   E:\VoxelForge-Engine
3. Accepter la fusion des dossiers.
4. Accepter le remplacement des fichiers existants.
5. Supprimer uniquement :
   E:\VoxelForge-Engine\build
   E:\VoxelForge-Engine\.vs
6. Rouvrir le projet dans Visual Studio.
7. Attendre la régénération CMake.
8. Compiler avec Ctrl + Shift + B.
9. Lancer VoxelForgeEditor.exe avec F5.
10. Vérifier la fenêtre, puis tester clavier, souris et molette.
11. Fermer la fenêtre avec la croix et vérifier le code 0.

Résultat attendu :
- compilation réussie ;
- fenêtre SDL3 toujours fonctionnelle ;
- aucun crash ;
- fermeture propre ;
- événements clavier et souris transmis au système Event.
