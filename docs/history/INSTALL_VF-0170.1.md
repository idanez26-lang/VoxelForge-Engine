# Installation — VF-0170.1 Layer System Fix

1. Arrêter le débogage.
2. Fermer complètement Visual Studio.
3. Décompresser ce ZIP dans :
   E:\VoxelForge-Engine
4. Accepter la fusion des dossiers.
5. Accepter le remplacement des quatre fichiers indiqués dans le manifeste.
6. Supprimer uniquement :
   - E:\VoxelForge-Engine\build
   - E:\VoxelForge-Engine\.vs
7. Rouvrir le projet dans Visual Studio.
8. Attendre la régénération CMake.
9. Compiler avec Ctrl + Shift + B.
10. Lancer VoxelForgeEditor.exe avec F5.

Résultat attendu :
- aucun crash dans xmemory ;
- aucune violation d'accès ;
- aucun avertissement d'initialisation multiple ;
- fermeture avec le code 0.
