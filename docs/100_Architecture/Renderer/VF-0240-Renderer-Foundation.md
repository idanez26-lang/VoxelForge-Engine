# VF-0240 — Renderer Foundation

## Objectif
Créer le module Renderer sans remplacer immédiatement le prototype ImGui.

Cette étape pose les contrats du futur backend GPU : API, configuration, cible de rendu, statistiques et cycle de frame.

## Choix prévu
Le premier backend visé est SDL GPU, déjà disponible via SDL3. Le reste du moteur restera indépendant de cette API.

## Étape suivante
VF-0241 créera réellement le périphérique SDL GPU et affichera son diagnostic.
