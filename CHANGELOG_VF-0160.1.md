# Changelog — VF-0160.1

## Corrigé

- suppression du changement automatique de répertoire de travail dans
  `Application::Initialize()`;
- suppression de l'appel problématique à `std::filesystem::current_path(...)`;
- correction du crash `0xC0000005` observé dans `SetCurrentDirectoryW`.

## Inchangé

- interface publique de `Application`;
- `ApplicationSpecification`;
- système d'événements;
- CMake;
- `editor/src/main.cpp`.
