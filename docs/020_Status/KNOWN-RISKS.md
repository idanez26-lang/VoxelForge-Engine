# Risques connus

Mis à jour le 31/07/2026 au soir (après vérification des tests V2 et épinglage des dépendances).

| Risque | Gravité | Mitigation / état |
|---|---|---|
| Chantier V2 non commité (uniquement sur le poste) | 🔴 | **Tests V2 vérifiés verts** (PencilCompactPlan, PencilInteractionV2, ViewportInteractionV2, ViewportInteractionV2Smoke — recompilés et exécutés le 31/07 au soir) → committer et pousser sans attendre |
| `EditorWorkspace.cpp` ≈ 17 400 lignes, en croissance (+2 400 en une semaine) | 🔴 | Règle « aucun nouveau code n'y entre » ; extraction par chantier avec tests avant/après (Phase C) |
| Aucun run CTest complet non interrompu documenté | 🟠 | CI prête (`ci.yml`) : build + ctest complet à chaque push dès qu'elle est poussée |
| Bug « grille qui disparaît à proximité » (§10.2 du registre) | 🟠 | Analyse code du 31/07 : aucune logique de disparition volontaire (pas de fade/LOD). Suspects si le bug persiste : clipping near plane (0,05) et précision depth buffer (far/near = 200 000:1 — z-fighting des lignes fines). Vérification visuelle au prochain smoke test ; correctif possible : near plane dynamique ou depth bias sur les guides |
| Performance sur grands modèles `.vox` non mesurée | 🟠 | Cibles benchmarks présentes (`VF_BUILD_BENCHMARKS=ON`) : exécuter p. ex. `build\windows-debug\benchmarks\VoxelForgeStampPlacementBenchmark.exe` et les benchmarks V2 (`ViewportInteractionV2SoakBenchmark`) sur un gros modèle |
| ~~Test `PencilCompactPlan` en échec le 31/07~~ | ✅ | **Résolu** — corrigé par Tony à 13h03, vérifié vert par recompilation/exécution le 31/07 au soir |
| ~~Dépendances FetchContent non épinglées~~ | ✅ | **Résolu** — `CMakeLists.txt` épinglé sur les commits exacts des tags (SDL3 `8e37db5e…`, ImGui `b61e5634…`, extraits du build local) ; à committer |
| Un seul poste de développement (E:\) | 🟡 | Push fréquents (acté le 31/07) ; envisager une sauvegarde disque |
| `ViewportRenderer.cpp` (≈ 2 500 l.) et `SmartToolPlanner.cpp` (≈ 1 500 l.) grossissent | 🟡 | Surveiller ; extraire des passes/stratégies à terme |
| Documentation utilisateur inexistante | 🟡 | Prévue en fin de v1.0 (roadmap) |
| Gestionnaire d'identifiants Windows en erreur `[0x8]` au push | 🟢 | Sans gravité (push OK) ; nettoyer les entrées `git:https://github.com` si gênant |
