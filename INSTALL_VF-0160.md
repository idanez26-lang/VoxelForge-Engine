# Installation — VF-0160 Application Framework

## Prérequis

- être sur la branche `feature/common-foundation` ;
- avoir déjà intégré VF-0150 Event System ;
- fermer Visual Studio avant la copie.

## Installation

1. Décompresser ce ZIP dans :

   `E:\VoxelForge-Engine`

2. Autoriser Windows à fusionner les dossiers.

3. Autoriser le remplacement de :

   - `engine/Core/include/VoxelForge/Core/Application.h`
   - `engine/Core/src/Application.cpp`

4. Le fichier suivant sera ajouté :

   - `engine/Core/include/VoxelForge/Core/ApplicationSpecification.h`

5. Rouvrir Visual Studio et lancer :

   `Ctrl + Shift + B`

## CMake

Aucune modification de `engine/Core/CMakeLists.txt` n'est nécessaire :

- `src/Application.cpp` y figure déjà ;
- `engine/Core/include` est déjà exposé publiquement ;
- `ApplicationSpecification.h` est un header uniquement.

## Validation

Le résultat attendu est :

`Tout générer : réussite.`

Le test `tests/Core/ApplicationFrameworkTests.cpp` est fourni mais n'est pas ajouté
automatiquement au build principal.
