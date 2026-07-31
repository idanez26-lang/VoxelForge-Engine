# Changelog — VF-0242.1 SDL Video Lifetime Fix

## Corrigé

Le périphérique SDL GPU était créé avant l'initialisation du sous-système vidéo,
ce qui provoquait :

`SDL GPU device creation failed: Video subsystem not initialized`

## Nouvelle responsabilité

- Renderer initialise et conserve le sous-système SDL vidéo ;
- SDLWindow utilise ce sous-système déjà actif ;
- SDLWindow détruit la fenêtre avant que Renderer détruise le périphérique GPU ;
- Renderer arrête SDL vidéo en dernier.
