# Risques connus

Mis à jour le **06/08/2026** (après ERGO-01 lot 2 et la revue adversariale à 14 agents).

| Risque | Gravité | Mitigation / état |
|---|---|---|
| **Régénérer un dossier de build existant n'écrit jamais `CMakeFiles/rules.ninja`** | 🔴 | Constaté le 05/08 sur les deux dossiers, avec et sans preset : `cmake` annonce « Build files have been written » et sort en code 0, puis `ninja` échoue sur `unknown build rule ..._unscanned_...`. Seule une génération **à neuf** écrit le fichier de règles. Recette qui marche : `cmake --fresh -S . -B <dir> -G Ninja -DCMAKE_BUILD_TYPE=<type> -DCMAKE_CXX_STANDARD=20 -DVF_BUILD_EDITOR=ON` (ajouter `-DVF_BUILD_BENCHMARKS=ON` pour les bancs, **OFF par défaut**). Ne pas utiliser `cmake --preset`, qui reproduit le défaut. Toujours vérifier la présence de la règle de toute nouvelle cible dans `rules.ninja` avant de compiler |
| **Ninja rate les dépendances d'en-tête sur les cibles de test** : un changement de taille de structure laisse des objets périmés | 🔴 | Coût réel : une demi-journée le 05/08 sur un faux SegFault (`StampLivePreview`), puis un blocage du même test. Dès qu'un en-tête change de **taille** de structure, purger les objets de notre code avant de compiler : `del /s /q build\<dir>\{editor,engine,tests}\*.obj` et `build\<dir>\CMakeFiles\*.obj`. Les dépendances externes (`_deps` : SDL3, ImGui) n'incluent aucun de nos en-têtes et sont à conserver |
| ~~`SmartToolExactPreviewComposer::Compose` copie tout le document puis remaille tout~~ | ✅ | **Corrigé les 05-06/08** (VF-0265, treize lots, puis LOT 4c). Cache d'overrides par chunk, signature de contenu par chunk, région localisée, et envoi GPU sauté pour les chunks inchangés. Mesuré en session réelle : `hl-plan` 13,8 → **0,2 ms**, `ho-exact` 10,1 → **0,0 ms**. Le plafond `MaximumExactPreviewDocumentVoxelCount` (50 000 voxels de **document**) est supprimé : il mesurait la mauvaise quantité, et même à l'envers — dix mille voxels épars coûtaient 2,3 ms quand un million de voxels denses en coûtait 0,05. Remplacé par `MaximumExactPreviewDeltaVoxelCount` = 8 192, sur le **volume du delta**, seule quantité qui gouverne le coût |
| ~~`StampPlacementPlanner::Build` parcourt tout le document~~ | ✅ | **Corrigé le 05/08** (PERF-FOUNDATION lot 2). Cache d'indices de palette occupés détenu par la session, invalidé sur identité + révision du document uniquement. Mesuré : 67,4 ms → **0,066 ms** à 1 M ; 7 exécutions sur 7 au-dessus du budget de frame → 1 (le premier relevé). Sans cache, le parcours de référence subsiste pour les appelants ponctuels : une seule implémentation, aucun risque de divergence |
| Un chunk 32³ coûte **28 ms** à remailler sur document dense | 🟠 | Découvert par VF-0264 §4 : c'est 1,7 frame pour un seul chunk, et le temps est stable quand le document double (28,36 ms à 512 k, 28,41 ms à 1 M). La granularité de 32 est trop grosse pour une interaction continue ; contrainte structurante de VF-0265 (recomposer à la taille du pinceau, pas du chunk) |
| Documents **grands mais peu peuplés** : `sparse10k` est le cas dimensionnant de la composition | 🟠 | **Expliqué et quantifié le 06/08** (`build\ergo01-lot0.csv`). Le coût est dominé par le nombre de **faces du chunk touché**, pas par la taille du document : un chunk au cœur d'un modèle dense n'a presque aucune face, tout étant masqué par ses voisins, tandis que des voxels épars en exposent le maximum. Pour un delta de 27 voxels : dense 1 M = **0,053 ms**, dense 15 625 = 0,484 ms, `sparse10k` en 64³ = **2,283 ms**. Le plus gros document est le moins cher. Conséquence : tout raisonnement de coût fondé sur le nombre de voxels du document est faux |
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

## ROUGE — Chemin de chaîne de compilation codé en dur (06/08/2026)

**Symptôme observé.** `cmake --build` échoue sur
`no such file or directory` suivi de
`CMake Error: Generator: build tool execution failed, command was:
E:/VISUAL~1/.../CMake/Ninja/ninja.exe`. Le message accuse ninja et ne dit rien
de la vraie cause.

**Cause.** Visual Studio avait été désinstallé. Les trois `CMakeCache.txt`
(`windows-debug`, `windows-release`, `perf-release`) gardent un
`CMAKE_MAKE_PROGRAM` absolu vers l'installation disparue, et tous nos scripts
appelaient `E:\visual studio\VC\Auxiliary\Build\vcvars64.bat` en dur.

**Correctif appliqué.** `build\vf-env.bat` interroge `vswhere` — l'annuaire
officiel des installations, qui survit aux désinstallations et couvre les Build
Tools seuls via `-products *` — puis replie sur l'ancien chemin. Tous les
scripts l'appellent par `call "%~dp0vf-env.bat"` et s'arrêtent avec un message
explicite si rien n'est trouvé, au lieu d'échouer plus loin sur un symptôme
trompeur.

**Règle.** Après tout changement d'installation de la chaîne, `cmake --fresh`
est obligatoire sur CHAQUE dossier de build : le cache retient un chemin absolu
que la reconfiguration ordinaire ne réécrit pas.


## Revue adversariale du 06/08/2026 — 8 bugs confirmés, aucun n'était connu

Méthode : 7 sous-systèmes, 14 agents (7 relecteurs, 7 vérificateurs). Chaque bug
a été rouvert par un second agent chargé de le **réfuter** ; tous sont ressortis
confirmés, zéro faux positif. Deux findings ont vu leur chaîne causale corrigée
par le vérificateur — un rapport qui se corrige lui-même.

**Aucun de ces huit défauts ne figurait dans ce registre ni ailleurs dans la
documentation.** Vérifié le 06/08 : les vingt entrées ci-dessus n'en couvraient
aucun. La seule mention de « palette 0 » (AR-0103) parle d'autre chose — que
l'index 0 ne peut pas représenter une cellule vide.

| # | Sous-système | Gravité | Nature | État |
|---|---|---|---|---|
| 1 | SmartTools (Fill) | 🔴 | Lecture hors-bornes, **comportement indéfini** | À corriger en priorité |
| 2 | ViewportRenderer | 🔴 | **Écriture GPU hors-bornes** | À corriger en priorité |
| 3 | Asset/IO `.vox` | 🟠 | **Perte de données** | À corriger |
| 4 | VoxelStamps | 🟠 | Blocage de session | À corriger |
| 5 | Selection V2 | 🟠 | Correction logique | À corriger |
| 6-7 | Palette | 🟠 | Validation | À corriger |
| 8 | Serializer | 🟡 | Gestion d'erreur | À corriger |

### 1 — 🔴 Lecture hors-bornes dans Smart Fill

`editor/src/SmartTools/SmartToolPlanner.cpp:779`. `CalculateBounds` (l. 167) fait
`positions.front()` sans vérifier le vide. Il est appelé à **cinq** endroits :
303, 429, 470, 779, 828. **Quatre ont une garde `if (result.Positions.empty())`
juste avant ; le site 779, dans `ResolveFill`, est le seul sans.** L'asymétrie
dans un même fichier est la preuve, et l'uniformité est le correctif — ne pas
inventer une troisième forme de garde.

Scénario : un voxel isolé sur le bord (X=0), normale de face verrouillée
`{-1,0,0}` → la région ne contient que la graine, la destination sort de la
grille, `Positions` reste vide. Le `try/catch` de `Plan()` **ne protège pas** :
un comportement indéfini n'est pas une exception.

### 2 — 🔴 Écriture GPU hors-bornes au changement d'outil

`editor/src/ViewportRenderer.cpp:1086`. Les tampons de highlights sont
**partagés** entre le chemin legacy et le chemin InteractionV2. Le chemin legacy
(`UploadBufferPair`) réalloue à la taille exacte **sans mettre à jour les
compteurs de capacité V2**. Au retour sur un outil V2, le test de croissance
conclut qu'aucune réallocation n'est nécessaire et `SDL_UploadToGPUBuffer` écrit
dans le petit tampon legacy.

Scénario : grande sélection V2 (tampon 8192) → bascule vers Crayon ou Boîte en
survolant le modèle, donc sans remise à zéro → retour en Sélection V2 avec une
taille intermédiaire → environ 4 000 octets écrits dans un tampon de 300.

**Le plus délicat des deux.** Établir par lecture quels compteurs existent, qui
les met à jour, et quels chemins d'envoi partagent quels tampons — **avant** de
modifier.

### 3 — 🟠 Fichiers `.vox` ouverts mais non ré-enregistrables

`engine/Asset/src/Voxel/VoxDocumentLoader.cpp:119` accepte jusqu'à
`Vox::MaximumVoxDimension` = **2048** (`VoxFormat.h:13`), alors que l'écrivain
refuse tout axe au-delà de `MaximumXyziCoordinate + 1` = **256**
(`VoxDocumentWriter.h:43`). Un `.vox` tiers dont un axe est entre 257 et 2048 se
charge, se modifie, puis **échoue définitivement à la sauvegarde** : les éditions
sont perdues. Le plafond 2048 est simplement la mauvaise valeur.

### 4 — 🟠 Drapeau de ré-entrance bloqué à `true` sur exception

`editor/src/VoxelStamps/Workflow/StampPreviewController.cpp:308`. `Place()` pose
`editInProgress_ = true` — **drapeau partagé par référence** avec les autres
opérations d'édition — puis appelle `PlaceOnce()` sans `try/catch` ; le reset à
`false` (l. 324) n'est que sur le chemin nominal, et aucune de ces opérations
n'est `noexcept`. Un `std::bad_alloc` sur un gros stamp saute le reset et
**désactive toute édition pour le reste de la session**. Correctif : un garde
RAII.

### 5 — 🟠 Sélection rectangle corrompue près du plan proche

`editor/src/ViewportInteractionV2/SelectionProjectionCache.cpp:92`. L'empreinte
écran d'un voxel agrège ses huit coins projetés, mais `TransformPoint` ne renvoie
`NaN` que si `|w| ≤ 1e-8` : pour un `w` **négatif** il renvoie un résultat fini
au signe inversé. Les coins derrière la caméra polluent donc `screenMin/Max` —
il manque un clipping au plan proche. Caméra dans le modèle, cube à cheval sur le
plan proche : des voxels entrent ou sortent de la sélection à tort.

### 6-7 — 🟠 Index de palette 0 accepté puis rejeté

**Chaîne causale corrigée par le vérificateur.** Les commandes
`AddVoxelCommand.cpp:27` et `PaintVoxelCommand.cpp:27` ne valident que la borne
haute, mais **n'atteignent jamais** `document->SetVoxel` — elles ne servent que
sans document actif. La vraie cause racine est
`PaintPaletteSelection.h:36`, qui initialise l'index à 0 et autorise
`SetIndex(0)` alors que le swatch 0 est sélectionnable et cliquable. Peindre ou
ajouter avec le swatch 0 passe par l'historique, `ValidateVoxelChanges` rejette
(« palette indices between 1 and 255 »), et **l'action échoue silencieusement**.

### 8 — 🟡 Échec de suppression du backup signalé comme échec de sauvegarde

`engine/Voxel/src/VoxelModelSerializer.cpp:619`. Après un rename atomique réussi
— les données **sont** persistées — si `remove(backup)` échoue, la fonction
renvoie `{false, ...}`. Le message le dit lui-même : « VFVOXEL saved, but its
backup could not be removed ». Faux négatif : l'utilisateur croit sa sauvegarde
perdue. Pire, le `.bak` résiduel déclenche la garde « stale artifact » (l. 538) à
chaque sauvegarde suivante vers ce chemin → **blocage permanent**. Atteignable
sur Windows (verrou d'antivirus ou d'indexeur sur le `.bak`).

## Limite structurelle de la suite de tests (constatée le 06/08)

Les 149 tests bloquants vérifient l'**état du document après commit**. Les 51
smokes vérifient que l'éditeur **ne casse pas**. **Aucun ne vérifie qu'on voit
quelque chose pendant le geste.**

Deux défauts visibles en usage normal ont donc traversé toute la suite : les
voxels d'un trait maintenu n'apparaissaient qu'au relâchement sur un 64³, et un
trait long ne présentait plus rien du tout au-delà du plafond de delta. Les deux
ont été trouvés par Tony en quelques secondes d'usage.

Conséquence de méthode : pour tout lot portant sur ce qui est **affiché**, la
validation est visuelle par construction. Ne pas attendre des tests qu'ils la
remplacent.
