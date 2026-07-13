# VF-0130 — Core Utilities

**Version :** 0.0.3  
**Statut :** Implémenté

## Composants

- `UUID` : identifiant 64 bits pour les objets métier.
- `Time` : date et heure au format ISO 8601.
- `FileSystem` : opérations de fichiers minimales et sûres.

## Règles

- Les utilitaires du Core ne dépendent d’aucun module supérieur.
- Les erreurs de fichiers sont signalées par une valeur de retour.
- Les identifiants sont générés localement sans service externe.
