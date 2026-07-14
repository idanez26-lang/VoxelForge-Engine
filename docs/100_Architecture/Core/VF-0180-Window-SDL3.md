# VF-0180 — Window SDL3

- Interface publique `Window` indépendante de SDL.
- Backend privé `SDLWindow`.
- SDL3 récupéré avec CMake FetchContent.
- Version épinglée à `release-3.4.10`.
- Gestion de la fermeture, du redimensionnement, du focus et du déplacement.
- Boucle principale graphique dans `Application::Run()`.
