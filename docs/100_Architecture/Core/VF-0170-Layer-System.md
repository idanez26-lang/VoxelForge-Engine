# VF-0170 — Layer System

## Objectif

Introduire une pile de couches possédée par `Application`.

## Concepts

- **Layer** : unité logique recevant les mises à jour, événements et appels UI.
- **Overlay** : couche prioritaire placée après les couches normales.
- **LayerStack** : propriétaire exclusif des couches avec `std::unique_ptr`.

## Cycle de vie

- `OnAttach()` lors de l'ajout.
- `OnDetach()` lors du retrait ou de la destruction.
- `OnUpdate()` pendant la mise à jour de l'application.
- `OnEvent()` en ordre inverse, overlays en premier.
- `OnImGuiRender()` pour la future interface.

## Sécurité

- aucune couche n'est acceptée avec un pointeur nul ;
- aucune copie du stack ou des couches ;
- la propriété est explicite avec `std::unique_ptr` ;
- les événements déjà traités ne sont pas redistribués.

## Intégration Application

`Application` expose :

- `PushLayer`
- `PushOverlay`
- `PopLayer`
- `PopOverlay`
- `GetLayerStack`

En attendant VF-0180 Window, `Run()` réalise une mise à jour unique.
