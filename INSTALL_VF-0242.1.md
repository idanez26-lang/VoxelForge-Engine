# Installation — VF-0242.1

1. Fermer Visual Studio.
2. Décompresser dans :
   E:\VoxelForge-Engine
3. Accepter les remplacements.
4. Supprimer :
   E:\VoxelForge-Engine\build
   E:\VoxelForge-Engine\.vs
5. Rouvrir Visual Studio.
6. Compiler avec Ctrl + Shift + B.
7. Lancer avec F5.

Résultat attendu :
- plus d'erreur « Video subsystem not initialized » ;
- l'éditeur s'ouvre ;
- Console : « Interface rendue avec SDL GPU » ;
- Backend : direct3d12 ;
- fermeture avec le code 0.
