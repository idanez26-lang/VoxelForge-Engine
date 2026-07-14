# Installation — VF-0241 SDL GPU Device

1. Fermer complètement Visual Studio.
2. Décompresser le ZIP dans :
   E:\VoxelForge-Engine
3. Accepter la fusion et le remplacement.
4. Supprimer uniquement :
   E:\VoxelForge-Engine\build
   E:\VoxelForge-Engine\.vs
5. Rouvrir Visual Studio.
6. Attendre la configuration CMake.
7. Compiler avec Ctrl + Shift + B.
8. Lancer avec F5.

Résultat attendu dans la Console intégrée :

[INFO] SDL GPU device actif.
Backend : direct3d12 ou vulkan
Shaders : DXIL, DXBC ou SPIR-V

Le Viewport filaire doit continuer à fonctionner.

Après validation :

git add .
git commit -m "[Renderer] Add VF-0241 SDL GPU device"
git push
