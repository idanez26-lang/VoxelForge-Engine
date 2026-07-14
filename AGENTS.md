# VoxelForge Studio — Instructions pour les agents

## Vision du projet

VoxelForge Studio est un logiciel de création voxel assistée.

Principe fondateur :

« Créer plus vite. Rester l’artisan. »

Le logiciel doit accélérer le travail de l’artiste, automatiser les tâches répétitives, proposer des bases et des variantes, mais ne jamais remplacer le contrôle créatif de l’utilisateur.

## Organisation de l’équipe

Tony est le directeur créatif et décide des objectifs, des fonctionnalités et de la direction artistique.

ChatGPT agit comme architecte logiciel, chef de projet technique et relecteur. Il prépare les missions, définit l’architecture et vérifie les résultats.

Codex agit comme développeur local. Il analyse le dépôt, modifie les fichiers, lance les commandes, compile, teste et produit un rapport.

## Méthode de travail obligatoire

1. Lire entièrement la tâche avant de modifier quoi que ce soit.
2. Examiner les fichiers concernés et leurs dépendances.
3. Vérifier les chemins d’accès avant toute création ou modification.
4. Travailler par petites étapes vérifiables.
5. Ne jamais modifier un fichier sans comprendre son rôle.
6. Ne jamais supprimer un fichier source sans autorisation explicite.
7. Ne jamais travailler en dehors du dossier du dépôt.
8. Ne jamais publier, pousser ou fusionner sur GitHub sans confirmation explicite.
9. Ne jamais utiliser `git reset --hard`, `git clean`, `rm`, `rmdir`, `del` ou `Remove-Item` sur des fichiers importants sans confirmation.
10. Avant un changement important, vérifier `git status` et proposer un commit de sauvegarde.
11. Après chaque modification, lancer la configuration, la compilation et les tests disponibles.
12. En cas d’erreur, analyser la cause exacte avant de corriger.
13. Ne pas masquer une erreur avec une solution temporaire fragile.
14. Préserver la compatibilité avec Windows et Visual Studio.
15. Garder le code lisible, modulaire et documenté.

## Validation obligatoire

Une tâche n’est considérée comme terminée que si :

- la configuration CMake réussit ;
- la compilation réussit ;
- les tests disponibles réussissent ;
- `git status` est vérifié ;
- la liste des fichiers modifiés est fournie ;
- les commandes exécutées sont indiquées ;
- les erreurs restantes sont signalées honnêtement.

## Rapport de fin de tâche

À la fin de chaque mission, fournir :

1. Résumé du travail réalisé.
2. Liste des fichiers créés.
3. Liste des fichiers modifiés.
4. Liste des fichiers supprimés, s’il y en a.
5. Commandes exécutées.
6. Résultat de la compilation.
7. Résultat des tests.
8. Problèmes restants.
9. Étape suivante recommandée.

## Interdictions

- Ne pas inventer qu’une compilation a réussi.
- Ne pas annoncer qu’un test est réussi sans l’avoir exécuté.
- Ne pas supprimer les fichiers personnels de Tony.
- Ne pas modifier automatiquement l’architecture générale sans validation.
- Ne pas installer de dépendance inconnue sans expliquer son utilité.
- Ne pas envoyer de code ou de données vers un service externe sans autorisation.
- Ne pas ajouter de fichiers temporaires Visual Studio dans Git.
