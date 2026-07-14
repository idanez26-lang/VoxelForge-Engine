# VF-0160 — Application Framework

## Objectif

Faire évoluer la classe `VoxelForge::Core::Application` existante sans changer
l'organisation actuelle du module Core.

## Choix d'intégration

Le projet possède déjà :

- `engine/Core/include/VoxelForge/Core/Application.h`
- `engine/Core/src/Application.cpp`
- une entrée dans `engine/Core/CMakeLists.txt`

VF-0160 conserve donc ces chemins. Aucun sous-module `Application/` supplémentaire
n'est créé, ce qui évite les doublons et les conflits de symboles.

## Fonctionnalités

- configuration via `ApplicationSpecification` ;
- nom d'application configurable ;
- répertoire de travail optionnel ;
- pause console configurable ;
- état `running_` ;
- fermeture explicite avec `Close()` ;
- réception des événements avec `OnEvent()` ;
- gestion de `WindowCloseEvent` ;
- interdiction de copier ou déplacer l'application.

## Dépendances

- Core Logger ;
- Core Version ;
- VF-0150 Event System ;
- bibliothèque standard C++20.

## Étape suivante

VF-0170 pourra ajouter `LayerStack` à cette classe sans changer son interface publique
principale.
