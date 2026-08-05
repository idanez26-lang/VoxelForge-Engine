# Risques connus

Mis à jour le 05/08/2026 (après STAMP-24 — rotation des Stamps sur les trois axes).

| Risque | Gravité | Mitigation / état |
|---|---|---|
| `SmartToolExactPreviewComposer::Compose` **copie tout le document** puis remaille tout, à chaque cellule survolée | 🟠 | Court-circuite le cache incrémental de VF-0262. Contenu aujourd'hui par un plafond (`MaximumExactPreviewDocumentVoxelCount` = 50 000) au prix de la préview exacte sur les gros modèles. Correctif réel = compositeur incrémental (« option B » de VF-0261, jamais implémentée) |
| `StampPlacementPlanner::Build` parcourt **tout le document** à chaque reconstruction d'aperçu, pour remplir 256 booléens de palette | 🟠 | O(N) à chaque mouvement de pointeur pendant un placement (×2 en mode PreviewAssist). Correctif : cache des index de palette occupés, invalidé par révision — mini-lot identifié |
| Rebuild complet du cache de chunks sur documents **grands mais peu peuplés** | 🟠 | L'heuristique `probeRegion` (`VoxelMeshBuilder.cpp`) bascule mal quand la population est inférieure au volume d'un chunk : traversée complète par chunk. Invisible en 64³ ; **à traiter avant de rendre les dimensions de création configurables** |
| `EditorWorkspace` : 7 200 lignes de code mais **649 variables membres** et 86 des 98 méthodes publiques dédiées aux smokes | 🟠 | VF-0260 a sorti la logique ; le reste est un harnais de test dans la classe. Prochaine étape naturelle : extraire l'état des smokes dans un `EditorWorkspaceSmokeHarness` ami (viderait ~450 membres de l'en-tête) |
| Bug « grille qui disparaît à proximité » (§10.2 du registre) | 🟠 | Jamais reproduit ni corrigé depuis le 31/07. Suspects : near plane (0,05) et précision du depth buffer (far/near ≈ 200 000:1). Correctif candidat : near plane dynamique ou depth bias sur les guides |
| Matérialisation du planner Smart Tool : ~21 Mo alloués par mouvement de pointeur à pinceau 64³ | 🟡 | 5 vecteurs O(volume) par plan. Chemin `PlanPencilCompact` (descripteur scalaire) existe déjà mais n'est utilisé que par Pencil V2 — piste : généraliser |
| Un seul poste de développement (E:\) | 🟡 | Push systématique après chaque lot (respecté) ; envisager une sauvegarde disque |
| `EditorWorkspaceSmoke.cpp` (7 346 l.) et `EditorWorkspace.cpp` (7 199 l.) restent les deux plus gros fichiers | 🟡 | Surveiller ; `ViewportRenderer.cpp` (2 564 l.) et `SmartToolPlanner.cpp` (1 456 l.) viennent ensuite |
| Rotation à 45° et rotation libre non disponibles | 🟡 | Arbitrage acté le 05/08 (DECISION-LOG) : 45° = rééchantillonnage assumé (STAMP-25) ; rotation libre = instances de scène, chantier d'architecture séparé |
| Entrée synthétique (pilotage assisté) intermittente sur les panneaux ImGui | 🟢 | Le survol passe toujours, les clics par intermittence. Sans effet sur l'usage humain ; contourner en validant à la main |
| ~~Chantier V2 non commité~~ | ✅ | Résolu — fusionné dans `feature/imgui`, branche supprimée |
| ~~`EditorWorkspace.cpp` ≈ 17 400 lignes~~ | ✅ | Résolu — **7 199 lignes** après VF-0260 (−58,6 %) |
| ~~Aucun run CTest complet documenté~~ | ✅ | Résolu — 144/144 en local à chaque lot ; CI verte, smokes GUI bloquants depuis `5650127` |
| ~~Performance sur grands modèles non mesurée~~ | ✅ | Résolu — VF-0262 : édit ~4-22 ms de 15k à 1M voxels (vs 836 ms de rebuild complet) ; benchmark `VoxelForgeIncrementalEditBenchmark` |
| ~~Documentation utilisateur inexistante~~ | ✅ | Résolu — `docs/200_Tools/Voxel-Stamps-V1-User-Guide.md` |
| ~~Gestionnaire d'identifiants Windows en erreur au push~~ | ✅ | Résolu — stockage DPAPI |
