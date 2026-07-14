# Installation — VF-0242 ImGui SDL GPU Backend

1. Fermer complètement Visual Studio.
2. Décompresser le ZIP dans :
   E:\VoxelForge-Engine
3. Accepter la fusion et les remplacements.
4. Supprimer uniquement :
   E:\VoxelForge-Engine\build
   E:\VoxelForge-Engine\.vs
5. Rouvrir Visual Studio.
6. Attendre la configuration CMake.
7. Compiler avec Ctrl + Shift + B.
8. Lancer avec F5.

Résultat attendu :
- l'éditeur s'ouvre normalement ;
- tous les panneaux sont visibles ;
- le Viewport prototype fonctionne toujours ;
- la Console affiche « Interface rendue avec SDL GPU » ;
- Backend : direct3d12 ;
- fermeture avec le code 0.

Après validation :

git add .
git commit -m "[Renderer] Migrate ImGui to SDL GPU backend"
git push
