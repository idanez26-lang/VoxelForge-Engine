# VF-0000 — Product Requirements Document

**Version :** 0.1  
**Statut :** Fondation validée

## 1. Définition

VoxelForge Engine est une plateforme universelle de création voxel. Elle doit permettre de produire n’importe quel type de contenu voxel : objets, bâtiments, personnages, végétation, véhicules, environnements et scènes complètes.

Le produit n’est pas conçu autour de Teardown, Blender, Unity, Unreal ou d’un autre outil. Ces logiciels sont des cibles possibles via des profils d’export.

## 2. Problème résolu

La création voxel professionnelle demande souvent plusieurs logiciels, des opérations répétitives et beaucoup de temps technique. VoxelForge doit réduire cette friction sans supprimer le rôle de l’artiste.

## 3. Public cible

- artistes voxel ;
- moddeurs ;
- développeurs de jeux ;
- studios indépendants et professionnels ;
- créateurs de contenu ;
- designers ;
- étudiants et enseignants ;
- débutants souhaitant apprendre.

## 4. Proposition de valeur

L’utilisateur décrit une intention, ajoute éventuellement des références, obtient une base voxel de qualité, puis la modifie librement.

## 5. Entrées prévues

### V1
- texte ;
- image ;
- plusieurs images ;
- texte + image ;
- création manuelle.

### Futures versions
- croquis ;
- plans ;
- vidéo ;
- scan smartphone ;
- LiDAR.

## 6. Sorties

- projet VoxelForge ;
- VOX ;
- OBJ ;
- GLTF ;
- autres formats par plugins et profils.

## 7. Principes

- l’IA assiste mais ne remplace pas l’artiste ;
- le moteur doit fonctionner sans IA ;
- les décisions artistiques importantes restent validables ;
- chaque génération est reproductible ;
- les modifications doivent être non destructives ;
- l’architecture reste indépendante des cibles d’export ;
- les styles, palettes et connaissances sont chargés comme données.

## 8. Critère de réussite de la première version

Un utilisateur doit pouvoir créer un asset voxel de qualité professionnelle, l’éditer, comparer des variantes et l’exporter sans maîtriser plusieurs logiciels spécialisés.

## 9. Hors périmètre V1

- villes complètes ;
- mondes entiers ;
- cloud ;
- collaboration temps réel ;
- marketplace ;
- génération vidéo avancée ;
- multi-GPU avancé.
