# Grand Livre de la Forge — Vision VoxelForge Studio

| Champ | Valeur |
|---|---|
| Projet | VoxelForge Studio / VoxelForge Engine |
| Statut | Vision consolidée — validation Tony requise |
| Date | 2026-07-31 |
| Principe produit | **Créer plus vite. Rester l'artisan.** |

## 1. Ce que nous construisons

VoxelForge Studio est un atelier professionnel de création voxel. La v1.0 est
un **éditeur d'abord** : un outil dans lequel un artiste crée, édite, transforme
et sauvegarde des modèles voxel avec un plaisir et une précision supérieurs aux
outils existants — sans aucune dépendance à l'IA.

La génération assistée (texte, image, Intent Engine) reste la destination à
long terme décrite dans le PRD (VF-0000), mais elle vient **après** la v1.0 :
on ne pose pas un générateur sur un atelier bancal.

## 2. Le filtre de toute décision

> **« Créer plus vite. Rester l'artisan. »**

Chaque fonctionnalité doit soit accélérer le geste de création, soit renforcer
le contrôle de l'artiste. Une fonctionnalité qui ne fait ni l'un ni l'autre
n'entre pas dans la v1.0.

## 3. Piliers UX non négociables

1. **Preview exacte avant toute action multi-voxel.** Ce que l'utilisateur voit
   est exactement ce qui sera appliqué (vert = valide, orange = avertissement,
   rouge = invalide). Le clic final ne recalcule rien (plan immuable, VF-0300).
2. **Placement libre par défaut.** Les contraintes (grille, axes, rotation)
   sont optionnelles et ne bloquent jamais.
3. **Peu d'outils, puissants, avec options riches.** La Toolbar permanente se
   limite à Smart Tool, Selection, Transform ; le reste est contextuel.
4. **Face Tools de premier rang.** Travailler les faces est un mode de première
   classe, pas une option cachée.
5. **Non-destruction.** Preview / Validation / Transaction / Undo systématiques
   (Constitution, art. 5 ; ADR-0003).
6. **Maître Eldor** sera le seul guide IA visible (modes Support/Forge) —
   idée future, hors périmètre v1.0.

## 4. Chaîne d'édition canonique

Tout geste d'édition suit la même chaîne :

```text
Input -> Tool Intent -> Preview -> Validation -> Transaction -> Document -> History -> Render Sync
```

Les chemins parallèles hérités doivent converger vers cette chaîne (chantier
Phase B). Aucun nouveau chemin d'édition ne doit la contourner.

## 5. Fondations toujours en vigueur

Les documents fondateurs restent la loi du projet et ne sont pas remplacés par
ce livre : VF-0000 (PRD), VF-0001 (Constitution), VF-0002 (ce que VoxelForge ne
fera jamais), ADR-0001 (modularité), ADR-0002 (l'IA est optionnelle), ADR-0003
(contrôle final de l'artiste). Le présent document précise seulement l'ordre de
bataille : **l'atelier avant le générateur**.

## 6. Discipline de vérité

Toute affirmation sur l'état du projet distingue :
imaginé / validé / documenté / codé / compilé / testé / commité / poussé.
Aucun commit, push, merge ou suppression sans validation explicite de Tony.
