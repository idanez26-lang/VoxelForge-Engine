# Changelog — VF-0160 Application Framework

## Ajouté

- `ApplicationSpecification`
- configuration du nom de l'application
- répertoire de travail optionnel
- pause console configurable
- `Application::Close()`
- `Application::OnEvent()`
- gestion de `WindowCloseEvent`
- accès en lecture à la configuration et à l'état d'exécution
- test autonome
- documentation d'architecture

## Modifié

- `Application.h`
- `Application.cpp`

## Non modifié

- `CMakeLists.txt` racine
- `engine/Core/CMakeLists.txt`
- `editor/src/main.cpp`
