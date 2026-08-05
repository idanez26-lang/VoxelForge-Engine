# VF-0251 — Voxel Stamps Implementation Plan V1

## Architecture Status

- **Document type:** implementation plan
- **Plan version:** V1
- **Architecture source:** `VF-0250-Voxel-Stamps-Architecture-V2.md`
- **Architecture status:** Approved
- **Implementation status:** Complete — STAMP-01 to STAMP-23 delivered
  (see VF-0252 for the performance baseline and VF-0253 for the release gate).
  Follow-up outside this plan: STAMP-24 (rotation around the three grid axes,
  delivered 05/08/2026) and STAMP-25 (45-degree resampled rotation, planned).
- **Target branch:** `feature/imgui`
- **Primary platform:** Windows, MSVC, Ninja, Direct3D 12
- **Product principle:** *Créer plus vite. Rester l’artisan.*

---

## 0. Purpose

This document converts the approved Voxel Stamps V2 architecture into a
sequence of small, independently reviewable implementation missions.

It is intentionally an implementation plan, not a second architecture. The
following decisions are therefore fixed and must not be reopened by an
implementation lot:

- the user-facing feature is named **Voxel Stamps**;
- the right-side experience is the **Forge Library**;
- Project Library and My Library are distinct storage scopes;
- `.vfstamp` is the portable source asset;
- the catalogue is derived, local, rebuildable, versioned and atomically
  written;
- Smart Placement is enabled by default but remains non-constraining;
- placement overlap is allowed by default with
  `OverwriteOverlapping`;
- scale is exactly `1:1` in V1;
- Auto Pivot is deterministic;
- variant selection is reproducible;
- one placement gesture produces one atomic Undo operation;
- the assets on disk remain the source of truth.

The plan does not authorize implementation. Every lot requires a separate
mission and explicit validation.

---

## 1. Repository baseline

### 1.1 Verified current components

The paths in this section exist in the repository at the time this plan was
written.

| Area | Existing path | Current responsibility | Reuse for Stamps | Current limitation |
|---|---|---|---|---|
| Voxel document | `engine/Asset/include/VoxelForge/Asset/Voxel/VoxelDocument.h` | Sparse voxel sub-models, dimensions, bounds, 256-color palette, voxel and palette mutations, revision and dirty state | Canonical active-document data source and destination | No compound palette-plus-voxel history payload |
| Voxel document implementation | `engine/Asset/src/Voxel/VoxelDocument.cpp` | Validated mutations and atomic `ApplyVoxelChanges` | Final application primitive for voxel changes | Palette mutation is separate from `ApplyVoxelChanges` |
| VOX loading | `engine/Asset/include/VoxelForge/Asset/Voxel/VoxDocumentLoader.h` | Builds a `VoxelDocument` from `.vox` data | Reference for validated document construction | Not a `.vfstamp` reader |
| Document session | `editor/src/VoxelDocument/VoxelDocumentSession.h` | Owns the active internal voxel document and document generation | Supplies active document, generation and project confinement | Supports voxel documents, not external reusable assets |
| Selection domain | `editor/src/Selection/SelectionService.h` | Sorted sparse selected positions, bounds, editable bounds and document generation | Sufficient source selection for V1 capture | No Stamp-specific capture result or pivot context |
| Selection interaction | `editor/src/Selection/SelectionInteraction.h` | Mouse interaction for selection volume and bounds | Existing selection UX remains unchanged | Must not become responsible for Stamp saving |
| Selection cache | `editor/src/Selection/SelectionVolumeCache.h` | Avoids repeated sparse selection-volume rebuilds | Performance model to follow for capture and preview caches | Not a reusable asset cache |
| Transform preview | `editor/src/Transform/TransformPreviewModel.h` | Selection-based transform ghost, collision and out-of-bounds data | Useful behavior and performance reference | Coupled to `SelectionService`, source voxels and transform semantics |
| Smart brush preview | `editor/src/SmartTools/SmartBrushPreviewResolver.h` | Resolves brush preview from Smart Brush output | Useful common-preview consumer reference | Brush-specific and not an external asset preview |
| Viewport rendering | `editor/src/ViewportRenderer.h` | Draws prepared viewport primitives and preview data | Render-only consumer for a future common preview contract | Must not calculate Stamp placement, collision or palette mapping |
| Transform pivot | `editor/src/Transform/TransformPivot.h` | `Center`, `Bottom`, `Top` transform pivot modes | Naming and presentation reference only | Missing `Auto`, `Surface`, `Corner`; float world position is not a portable Stamp pivot |
| Pivot manager | `editor/src/Transform/TransformPivotManager.h` | Cached pivot derived from selection bounds | Reference for deterministic caching | Selection-transform-specific; cannot own Stamp Auto Pivot policy |
| Voxel edit operation | `editor/src/VoxelHistory/VoxelEditOperation.h` | Label, voxel changes and optional selection transition | Existing atomic voxel history envelope | Cannot describe palette mutations or exact chosen variant identity |
| Voxel edit history | `editor/src/VoxelHistory/VoxelEditHistory.h` | Undo/Redo stacks, 100-command and 256 MiB limits, saved-state tracking | Required history coordinator | Payload extension is required before palette-aware Stamp placement |
| Edit transaction | `editor/src/Commands/Voxel/VoxelEditTransaction.h` | Applies voxel changes forward/backward through `VoxelEditSession` | Low-level application pattern | Voxel changes only |
| Edit session | `editor/src/Commands/Voxel/VoxelEditSession.h` | Active document/model, mesh rebuild and edit completion | Existing one-rebuild application path | No palette transaction callback |
| Transform operation framework | `editor/src/Transform/TransformOperationFramework.h` | Validates immutable transform previews and builds atomic operations | Pattern for planner → immutable operation → history | Selection-transform-specific |
| Duplicate operation | `editor/src/Transform/DuplicateVoxelSelectionOperation.h` | Atomic selected-voxel duplication | Closest placement behavior reference | Operates only on voxels already in the active document |
| Asset filesystem | `editor/src/AssetBrowser/AssetDirectory.h` | Safe project `Assets` navigation, rename and delete confinement | Path-safety and refresh patterns | Generic file browser, no Forge catalogue or My Library scope |
| Asset view model | `editor/src/AssetBrowser/AssetBrowserViewModel.h` | File-oriented filtering, sorting, search and grid/list state | UI convention reference | No UUID, tags, category hierarchy or catalogue query model |
| Asset metadata | `editor/src/ModelImport/ModelAssetMetadataService.h` | `.vfmeta` for imported VOX models, analysis and thumbnail metadata | Atomic metadata and rebuild patterns | VOX-model-specific; must not become the Stamp catalogue |
| Thumbnail metadata | `editor/src/Thumbnail/ThumbnailMetadata.h` | `.vfthumb` metadata and presentation state | Thumbnail status vocabulary and dimensions reference | Model-thumbnail-specific |
| Thumbnail generation | `editor/src/Thumbnail/VoxThumbnailService.h` | Project-local thumbnail generation, cache and orphan cleanup | Atomic cache-install and invalidation patterns | Reads VOX models, not `.vfstamp`; tied to project model metadata |
| Thumbnail GPU cache | `editor/src/Thumbnail/ThumbnailTextureCache.h` | Runtime thumbnail texture cache | UI adapter can reuse generic texture presentation patterns | GPU-dependent; forbidden in catalogue/domain code |
| Project root | `engine/Project/include/VoxelForge/Project/Project.h` | Stable project ID, root and project file | Canonical Project Library root | No personal-library root abstraction |
| Project manager | `engine/Project/include/VoxelForge/Project/ProjectManager.h` | Active project lifecycle | Supplies active project/root to editor integration | Does not manage Forge Library assets |
| Project session | `editor/src/ProjectSession/ProjectSessionService.h` | Per-project camera, tool, palette and Smart Brush session | Possible future placement preference reference | V1 must not persist Stamp rotation/mirror across restart |
| VOX save | `editor/src/VoxelSave/VoxelDocumentSaveService.h` | Verified temporary-write, replace and rollback workflow | Atomic filesystem-write pattern | Saves documents, not Stamp assets/catalogues |
| Generic voxel serialization | `engine/Voxel/include/VoxelForge/Voxel/VoxelModelSerializer.h` | Atomic binary `VFVOXEL` save/load | Chunk-validation and atomic replacement reference | Different runtime model and format contract |
| Build integration | `editor/CMakeLists.txt` | Editor source registration | Add Stamp modules per lot | Must remain minimal per commit |
| Test integration | `tests/CMakeLists.txt` | Test executables, CTest and smoke registrations | Add focused targets per lot | Avoid one monolithic Stamp test binary |
| Editor routing | `editor/src/EditorWorkspace.cpp`, `editor/src/EditorWorkspace.h` | Application-level UI and interaction routing | Final thin integration seam | Already large; no Stamp domain logic may be added here |
| Smoke entry point | `editor/src/main.cpp` | Command-line smoke modes | Register final Stamp smoke scenarios | No domain logic |

### 1.2 Existing test assets to preserve

The implementation must keep the following existing test families green:

- `tests/Editor/SelectionServiceTests.cpp`;
- `tests/Editor/TransformPreviewTests.cpp`;
- `tests/Editor/TransformOperationFrameworkTests.cpp`;
- `tests/Editor/VoxelEditHistoryTests.cpp`;
- `tests/Editor/AssetBrowserTests.cpp`;
- `tests/Editor/AssetMetadataTests.cpp`;
- `tests/Editor/VoxThumbnailTests.cpp`;
- `tests/Editor/VoxelDocumentSaveServiceTests.cpp`;
- `tests/Editor/ProjectSessionTests.cpp`;
- `tests/Asset/VoxelDocumentTests.cpp`;
- `tests/Voxel/VoxelModelSerializerTests.cpp`;
- `tests/Mesh/VoxelDocumentMeshSyncTests.cpp`.

The repository already has focused test executables in `tests/CMakeLists.txt`.
Stamp tests must follow the same pattern.

### 1.3 Existing implementation patterns to reuse

The following patterns are proven and should be reused without copying their
business logic:

1. **Sparse iteration:** `VoxelSubModel::ForEachVoxel` and
   `SelectionService::Voxels`.
2. **Document staleness:** document generation plus document revision.
3. **Immutable preview hand-off:** `TransformPreviewOperationData`.
4. **Atomic voxel application:** `VoxelEditHistory`,
   `VoxelEditTransaction` and one mesh rebuild through `VoxelEditSession`.
5. **Atomic filesystem installation:** write temporary, validate, move old
   destination to backup, install, rollback on failure, remove recognized
   temporary artifacts.
6. **Safe project confinement:** `VoxelDocumentSession` and `AssetDirectory`.
7. **Derived caches:** model `.vfmeta` and `.vfthumb` can be rebuilt from source
   assets.
8. **Application routing:** `EditorWorkspace` supplies context and invokes
   services; it must not calculate asset geometry.

### 1.4 Confirmed gaps against VF-0250

| Gap | Impact | Classification |
|---|---|---|
| No `VoxelStamp` domain | No portable reusable creation | Blocking |
| No `.vfstamp` serializer/validator | No source asset | Blocking |
| No Project/My Library repository abstraction | No safe portable storage | Blocking for the relevant scope |
| No Forge catalogue | No searchable/rebuildable library | Blocking for discovery |
| No common placement-preview contract | Stamp preview would duplicate brush/transform rendering paths | Blocking before live preview |
| History cannot own palette changes | Palette-aware placement cannot be fully atomic | Blocking before final placement |
| Existing transform pivot lacks Stamp modes/policy | Auto Pivot would be ambiguous | Blocking before capture/placement |
| No placement planner | Collision, palette, limits and bounds cannot be validated centrally | Blocking |
| No exact variant identity in operation metadata | Variant Redo could reroll | Blocking before Smart Variants |
| No My Library root provider | Personal cross-project library unavailable | Recommended after Project Library MVP |
| Asset Browser is file-oriented | Forge Library UX cannot be implemented by relabelling current entries | Blocking for final MVP discovery UI |
| VOX thumbnails are format-specific | Stamp cards need a Stamp thumbnail adapter | Recommended for polished MVP |

No existing subsystem named Project Library or My Library was found. They must
be introduced as explicit Stamp/Forge Library abstractions rather than inferred
from `AssetDirectory`.

---

## 2. Prerequisites and dependency classification

### 2.1 Blocking prerequisites

#### P-B1 — Minimal stable Selection contract

The current `SelectionService` is adequate for V1 if capture:

- consumes `SelectionService::Voxels()` as a read-only sparse span;
- verifies the active document generation and revision;
- requires a non-empty selection;
- reads voxel values from `VoxelDocument`;
- normalizes coordinates relative to the approved pivot;
- never stores a pointer/span beyond the capture call.

No Selection v2 rewrite is required for the MVP. A new Selection subsystem
becomes blocking only if the current generation/revision checks cannot produce
an immutable capture safely.

#### P-B2 — Common preview contract

The Stamp implementation must not add a fourth independent renderer-specific
preview model. A small common contract is required before live Stamp preview:

- immutable prepared voxel instances;
- palette/color data already resolved;
- semantic state per voxel or batch;
- bounds and counts;
- stable revision;
- no renderer dependency in the producer;
- no placement calculation in the renderer.

The existing transform and brush previews do not need to migrate completely in
the first lot. The contract must allow adapters so migration can be incremental.

#### P-B3 — Compound document edit

Stamp placement can introduce palette colors. One Undo must restore both:

- voxel occupancy/color indices;
- every palette entry changed by palette mapping.

The current `VoxelEditOperation` only stores voxel changes. A narrowly scoped
compound edit payload or a new document operation envelope is required before
palette-aware placement is considered valid.

#### P-B4 — Document command boundary

The planner must produce immutable data. Only a document command may:

- revalidate generation and revision;
- apply palette changes and voxel changes;
- trigger exactly one revision transition;
- trigger exactly one mesh rebuild;
- record one history entry;
- rollback completely on failure.

`EditorWorkspace` must not perform those steps directly.

#### P-B5 — Central resource-limit policy

Limits must be checked before allocation, decompression and expansion. The
policy belongs to Stamp domain/format code and is injected into readers,
capture and placement.

### 2.2 Recommended prerequisites

- A small generic atomic-file installer extracted from the proven save/cache
  workflows, if it can be done without destabilizing those workflows.
- A `UserDataPaths` provider for `%LOCALAPPDATA%/VoxelForge Studio`, injectable
  in tests.
- Stable UUID and content-hash helpers reused from existing Core facilities
  where possible.
- A generic thumbnail presentation adapter so Forge Library does not depend on
  `VoxThumbnailService`.
- A test fixture library for temporary projects, documents, selections and
  `.vfstamp` byte corruption.

### 2.3 Deferrable prerequisites

- Full Selection v2.
- Full migration of brush and transform previews to the common preview model.
- Background catalogue indexing.
- Database-backed catalogue.
- Custom pivots and anchors.
- Persistent per-asset placement transformations.
- Cloud synchronization.

---

## 3. Target implementation boundaries

### 3.1 Proposed source layout

The following paths are proposals for future lots. This document does not
create them.

```text
editor/src/VoxelStamps/
├── VoxelStamp.h
├── StampTypes.h
├── StampResourceLimits.h
├── StampResourceLimits.cpp
├── StampValidationService.h
├── StampValidationService.cpp
├── Capture/
│   ├── StampCaptureService.h
│   ├── StampCaptureService.cpp
│   ├── StampAutoPivotResolver.h
│   └── StampAutoPivotResolver.cpp
├── Format/
│   ├── VfstampFormat.h
│   ├── VfstampReader.h
│   ├── VfstampReader.cpp
│   ├── VfstampWriter.h
│   └── VfstampWriter.cpp
├── Library/
│   ├── IStampLibraryRepository.h
│   ├── StampLibraryPaths.h
│   ├── StampProjectLibraryRepository.h
│   ├── StampProjectLibraryRepository.cpp
│   ├── StampUserLibraryRepository.h
│   ├── StampUserLibraryRepository.cpp
│   ├── IStampCatalogStore.h
│   ├── StampJsonCatalogStore.h
│   ├── StampJsonCatalogStore.cpp
│   ├── StampCatalogService.h
│   ├── StampCatalogService.cpp
│   ├── StampAssetCache.h
│   └── StampAssetCache.cpp
├── Placement/
│   ├── StampPlacementPlanner.h
│   ├── StampPlacementPlanner.cpp
│   ├── StampPaletteMapper.h
│   ├── StampPaletteMapper.cpp
│   ├── StampPlacementSession.h
│   ├── StampPlacementSession.cpp
│   ├── PlaceVoxelStampOperation.h
│   └── PlaceVoxelStampOperation.cpp
├── Preview/
│   ├── VoxelPlacementPreview.h
│   ├── StampPreviewAdapter.h
│   └── StampPreviewAdapter.cpp
├── Variants/
│   ├── StampVariantResolver.h
│   ├── StampVariantResolver.cpp
│   ├── StampVariantGroup.h
│   └── StampVariantGroup.cpp
└── SmartPlacement/
    ├── StampSmartPlacementService.h
    └── StampSmartPlacementService.cpp

editor/src/ForgeLibrary/
├── ForgeLibraryViewModel.h
├── ForgeLibraryViewModel.cpp
├── ForgeLibraryPanel.h
├── ForgeLibraryPanel.cpp
├── SaveSelectionAsStampWorkflow.h
└── SaveSelectionAsStampWorkflow.cpp
```

### 3.2 Proposed test layout

```text
tests/Editor/
├── VoxelStampDomainTests.cpp
├── VfstampFormatTests.cpp
├── VfstampValidationTests.cpp
├── StampCaptureTests.cpp
├── StampAutoPivotTests.cpp
├── StampProjectLibraryTests.cpp
├── StampUserLibraryTests.cpp
├── StampCatalogTests.cpp
├── StampAssetCacheTests.cpp
├── VoxelPlacementPreviewTests.cpp
├── StampPlacementPlannerTests.cpp
├── PlaceVoxelStampOperationTests.cpp
├── StampPlacementSessionTests.cpp
├── ForgeLibraryViewModelTests.cpp
├── StampVariantTests.cpp
├── StampSmartPlacementTests.cpp
├── StampPerformanceTests.cpp
└── VoxelStampSmokeTests.cpp
```

### 3.3 Dependency direction

```text
UI / EditorWorkspace
        |
        v
Workflow + ViewModels
        |
        +-----------------------+
        v                       v
Library interfaces       Placement session
        |                       |
        v                       v
Format / catalogue       Planner / preview
        |                       |
        +-----------+-----------+
                    v
             VoxelStamp domain
                    |
                    v
      VoxelDocument command/history adapters
```

Rules:

- arrows point toward dependencies;
- no domain/library/planner header includes ImGui, SDL, D3D12 or renderer
  headers;
- `ViewportRenderer` consumes prepared preview data only;
- `EditorWorkspace` routes commands and displays results only;
- catalogue code never owns GPU resources;
- `.vfstamp` format code never reads project or UI state.

---

## 4. Implementation lots

Every lot below must compile, test and be reviewable independently. “Probable
files” is a planning forecast, not permission to touch all listed files.

### STAMP-01 — Domain Core and central limits

- **Goal:** introduce immutable Stamp value types and the configurable soft/hard
  resource-limit policy.
- **Probable files:** new `editor/src/VoxelStamps/VoxelStamp.h`,
  `StampTypes.h`, `StampResourceLimits.h/.cpp`;
  `editor/CMakeLists.txt`; `tests/CMakeLists.txt`;
  `tests/Editor/VoxelStampDomainTests.cpp`.
- **Public API:** `VoxelStamp`, `StampVoxel`, `StampPaletteEntry`,
  `StampIdentity`, `StampBounds`, `StampPivot`, `StampTransform`,
  `StampResourceLimits`, `EvaluateStampLimits`.
- **Dependencies:** Core UUID/hash utilities and voxel scalar types only.
- **Explicit exclusions:** serialization, filesystem, UI, placement.
- **Tests:** coordinate normalization, palette references, bounds, identity,
  equality, 1:1 scale rejection, soft warning, hard rejection, arithmetic
  overflow before allocation.
- **Acceptance:** domain headers contain no editor UI/render dependencies;
  default V1 thresholds match VF-0250 and are injectable in tests.
- **Risks:** accidental coupling to `VoxelDocument`; signed/unsigned overflow.
- **Size:** Medium.

### STAMP-02 — `.vfstamp` container primitives

- **Goal:** define chunk IDs, byte order, checked sizes, required/optional chunk
  rules and format-version constants without filesystem I/O.
- **Probable files:** new `editor/src/VoxelStamps/Format/VfstampFormat.h`,
  `VfstampReader.h/.cpp`; CMake/test registration;
  `tests/Editor/VfstampFormatTests.cpp`.
- **Public API:** `ReadVfstampBytes(span<byte>, limits)`,
  `VfstampReadResult`, typed format errors and warnings.
- **Dependencies:** STAMP-01.
- **Explicit exclusions:** asset installation, catalogue, thumbnails.
- **Tests:** valid minimum file, truncation at every boundary, duplicate
  required chunk, unknown optional chunk, unknown required chunk, integer
  overflow, excessive chunk count, hard limits before allocation.
- **Acceptance:** malformed input never causes unchecked allocation or partial
  domain output.
- **Risks:** parser complexity and denial-of-service input.
- **Size:** Medium.

### STAMP-03 — Deterministic writer and round trip

- **Goal:** serialize a valid `VoxelStamp` deterministically and read it back.
- **Probable files:** new `VfstampWriter.h/.cpp`; modifications to format tests.
- **Public API:** `WriteVfstampBytes(const VoxelStamp&, options)`,
  `VfstampWriteResult`.
- **Dependencies:** STAMP-01, STAMP-02.
- **Explicit exclusions:** writing to final library paths.
- **Tests:** byte-for-byte determinism, round trip, canonical chunk order,
  content hash stability, UTF-8 metadata, empty optional chunks, scale 1 only.
- **Acceptance:** equivalent domain values produce identical canonical bytes
  and content hash.
- **Risks:** nondeterministic map iteration; hash coverage mismatch.
- **Size:** Medium.

### STAMP-04 — Validation and compatibility service

- **Goal:** centralize semantic validation after parsing and define read/write
  compatibility behavior.
- **Probable files:** new `StampValidationService.h/.cpp`;
  `tests/Editor/VfstampValidationTests.cpp`.
- **Public API:** `ValidateStamp`, `ValidateStampForWrite`,
  `ValidateStampForPlacement`, `StampValidationReport`.
- **Dependencies:** STAMP-01 to STAMP-03.
- **Explicit exclusions:** destination-document collision and palette mapping.
- **Tests:** invalid UUID/hash, empty voxels, duplicate coordinates, invalid
  palette index, invalid pivot, unsupported major version, tolerated optional
  extension, soft/hard limits.
- **Acceptance:** writer, libraries and planner can call one semantic validator.
- **Risks:** validation duplicated later in consumers.
- **Size:** Small.

### STAMP-05 — Selection capture and deterministic Auto Pivot

- **Goal:** capture the current sparse selection as a normalized immutable
  `VoxelStamp` candidate and resolve the approved pivot deterministically.
- **Probable files:** new `Capture/StampCaptureService.h/.cpp`,
  `Capture/StampAutoPivotResolver.h/.cpp`;
  `tests/Editor/StampCaptureTests.cpp`,
  `tests/Editor/StampAutoPivotTests.cpp`.
- **Public API:** `StampCaptureRequest`, `StampCaptureResult`,
  `ResolveAutoPivot(StampPivotContext)`.
- **Dependencies:** STAMP-01, STAMP-04, `SelectionService`,
  `VoxelDocument`.
- **Explicit exclusions:** dialog, file writing, catalogue registration.
- **Tests:** non-empty sparse selection, missing/stale voxel, generation and
  revision mismatch, local-coordinate normalization, palette compaction,
  explicit pivot override, top horizontal → Bottom Center, vertical → Surface,
  free/ambiguous → Center, precise-grid → Corner, same input → same output.
- **Acceptance:** no selection/document span escapes the call; no document
  mutation; identical context yields identical pivot and bytes-ready domain.
- **Risks:** face-context availability from current selection interaction.
- **Size:** Medium.

### STAMP-06 — Project Library filesystem repository

- **Goal:** safely install/read/delete Project Library `.vfstamp` assets under
  the project root using an interface independent of UI.
- **Probable files:** new `Library/IStampLibraryRepository.h`,
  `StampLibraryPaths.h`, `StampProjectLibraryRepository.h/.cpp`;
  `tests/Editor/StampProjectLibraryTests.cpp`.
- **Public API:** `Install`, `Read`, `EnumerateSourceAssets`, `Remove`,
  `ResolvePortableReference`, `RebuildSourceInventory`.
- **Dependencies:** STAMP-02 to STAMP-04 and `Project::Project::RootPath`.
- **Explicit exclusions:** catalogue JSON, My Library, card UI.
- **Tests:** canonical project path, traversal/symlink escape rejection,
  unique filenames, atomic replacement/rollback, stale temp refusal,
  interrupted write recovery rules, source enumeration.
- **Acceptance:** all operation paths remain under
  `<ProjectRoot>/Assets/ForgeLibrary`; assets are the source of truth.
- **Risks:** Windows rename semantics and symlink/reparse-point confinement.
- **Size:** Medium.

### STAMP-07 — Rebuildable JSON catalogue

- **Goal:** implement the V1 versioned atomic catalogue backend and rebuild it
  entirely from Project Library source assets.
- **Probable files:** new `Library/IStampCatalogStore.h`,
  `StampJsonCatalogStore.h/.cpp`, `StampCatalogService.h/.cpp`;
  `tests/Editor/StampCatalogTests.cpp`.
- **Public API:** `LoadCatalogue`, `WriteCatalogueAtomically`,
  `RebuildCatalogue`, `Query`, `UpsertDerivedEntry`, `RemoveDerivedEntry`.
- **Dependencies:** STAMP-06.
- **Explicit exclusions:** SQLite, background indexing, GPU thumbnails.
- **Tests:** empty library, deterministic ordering, add/update/remove, corrupt
  catalogue recovery, complete rebuild, unknown catalogue version, atomic
  rollback, no absolute My Library path.
- **Acceptance:** deleting the catalogue and rebuilding produces equivalent
  searchable entries from the assets.
- **Risks:** catalogue mistakenly treated as authoritative; path leakage.
- **Size:** Medium.

### STAMP-08 — Save Selection As Stamp workflow

- **Goal:** connect selection capture, metadata input, writer, Project Library
  installation and catalogue refresh as one application workflow.
- **Probable files:** new
  `editor/src/ForgeLibrary/SaveSelectionAsStampWorkflow.h/.cpp`;
  minimal integration in `EditorWorkspace.cpp/.h`;
  focused dialog/view-model files if required;
  `tests/Editor/SaveSelectionAsStampTests.cpp`.
- **Public API:** `Begin`, `ValidateDraft`, `Save`, typed success/failure result.
- **Dependencies:** STAMP-05 to STAMP-07.
- **Explicit exclusions:** My Library destination, live placement, variants.
- **Tests:** empty selection refusal, valid save, name validation, duplicate
  name policy, capture failure leaves no files, catalogue failure recovery,
  exact pivot and metadata persisted.
- **Acceptance:** Selection → Save Selection As creates one valid Project
  Library asset discoverable after catalogue rebuild.
- **Risks:** `EditorWorkspace` absorbing business logic; partial asset/catalogue
  state.
- **Size:** Medium.

### STAMP-09 — My Library and portable scope references

- **Goal:** add a profile-level repository with injected user-data root while
  preserving explicit, portable scope references.
- **Probable files:** new `StampUserLibraryRepository.h/.cpp`,
  user-data path adapter; `tests/Editor/StampUserLibraryTests.cpp`.
- **Public API:** same `IStampLibraryRepository`; `StampLibraryScope`,
  `StampAssetReference {Scope, UUID, RelativePath}`.
- **Dependencies:** STAMP-06, STAMP-07.
- **Explicit exclusions:** sync, cloud, copying project dependencies
  automatically.
- **Tests:** injected `%LOCALAPPDATA%` equivalent, cross-project availability,
  profile confinement, project references contain no absolute profile path,
  missing personal asset is explicit.
- **Acceptance:** Project and My repositories are interchangeable through the
  interface but never silently cross-reference absolute paths.
- **Risks:** accidental use of the real user profile in tests.
- **Size:** Small.

### STAMP-10 — Stamp lazy-load and CPU cache

- **Goal:** avoid repeated parsing while keeping source files authoritative and
  invalidating by identity/content facts.
- **Probable files:** new `Library/StampAssetCache.h/.cpp`;
  `tests/Editor/StampAssetCacheTests.cpp`.
- **Public API:** `GetOrLoad(reference)`, `Invalidate`, `InvalidateScope`,
  `Metrics`.
- **Dependencies:** STAMP-04, STAMP-06, STAMP-09.
- **Explicit exclusions:** GPU textures and background threads.
- **Tests:** cache hit, source modification invalidation, corrupt reload,
  bounded memory, deterministic eviction, project switch.
- **Acceptance:** unchanged assets parse once; stale/corrupt assets are never
  returned as fresh.
- **Risks:** filesystem timestamp granularity; retaining oversized assets.
- **Size:** Small.

### STAMP-11 — Compound document edit foundation

- **Goal:** extend the document-edit boundary so one history operation can own
  palette and voxel mutations atomically.
- **Probable files:** modifications to
  `editor/src/VoxelHistory/VoxelEditOperation.h/.cpp`,
  `VoxelEditHistory.cpp`,
  `editor/src/Commands/Voxel/VoxelEditTransaction.h/.cpp`,
  possibly `VoxelEditSession.h`,
  `engine/Asset/.../VoxelDocument.h/.cpp`;
  additions to `tests/Editor/VoxelEditHistoryTests.cpp` and
  `tests/Asset/VoxelDocumentTests.cpp`.
- **Public API:** `VoxelPaletteChange`, compound `VoxelDocumentEdit`,
  forward/backward atomic apply.
- **Dependencies:** existing history/document only.
- **Explicit exclusions:** Stamp types, placement, UI.
- **Tests:** palette only, voxel only, combined operation, failure rollback,
  Undo/Redo, memory accounting, dirty/saved state, exactly one revision and mesh
  rebuild.
- **Acceptance:** a failed compound edit restores both palette and voxels;
  existing edit callers require no behavior change.
- **Risks:** highest regression risk in the plan; palette aliases in existing
  voxels; revision semantics.
- **Size:** Large.

### STAMP-12 — Common voxel placement preview contract

- **Goal:** define renderer-neutral preview data and adapters without rewriting
  existing brush/transform behavior.
- **Probable files:** new `Preview/VoxelPlacementPreview.h`;
  adapters beside existing preview producers; minimal
  `ViewportRenderer.h/.cpp` consumption changes;
  `tests/Editor/VoxelPlacementPreviewTests.cpp`.
- **Public API:** `VoxelPlacementPreview`, `VoxelPreviewInstance`,
  `VoxelPreviewSemantic`, `VoxelPreviewStats`, immutable spans/revision.
- **Dependencies:** existing preview systems.
- **Explicit exclusions:** Stamp planning, new visual effects, renderer business
  decisions.
- **Tests:** semantic colors/states, stable revision, empty preview, adapter
  equivalence, no document mutation, large-preview render policy.
- **Acceptance:** renderer consumes already prepared instances/states; current
  brush and transform visuals remain unchanged.
- **Risks:** widening a “common” API prematurely; render regression.
- **Size:** Medium.

### STAMP-13 — Palette mapper and placement planner

- **Goal:** produce a complete immutable placement plan with transformed
  positions, palette changes, overlap states, bounds and statistics.
- **Probable files:** new `Placement/StampPaletteMapper.h/.cpp`,
  `StampPlacementPlanner.h/.cpp`;
  `tests/Editor/StampPlacementPlannerTests.cpp`.
- **Public API:** `StampPlacementRequest`, `StampPlacementPlan`,
  `StampPlacementIssue`, `PlanPlacement`.
- **Dependencies:** STAMP-01, STAMP-04, STAMP-10, STAMP-11.
- **Explicit exclusions:** document mutation, UI, drawing.
- **Tests:** empty destination, palette reuse/new slots/exhaustion, default
  overwrite overlap, Reject, SkipOccupied, out-of-bounds refusal, rotations,
  mirrors, 1:1 scale, soft/hard limits, deterministic output.
- **Acceptance:** planning is pure with respect to the document and produces
  everything needed by preview and commit.
- **Risks:** palette exhaustion policy; coordinate overflow; incorrect overlap
  accounting.
- **Size:** Large.

### STAMP-14 — Exact live Stamp preview

- **Goal:** adapt a placement plan to the common preview contract and display
  exact voxels before mutation.
- **Probable files:** new `Preview/StampPreviewAdapter.h/.cpp`;
  minimal routing in `EditorWorkspace`; focused renderer adapter changes only
  if STAMP-12 requires them;
  tests in `StampPlacementPlannerTests.cpp` and
  `VoxelPlacementPreviewTests.cpp`.
- **Public API:** `BuildStampPreview(const StampPlacementPlan&)`.
- **Dependencies:** STAMP-12, STAMP-13.
- **Explicit exclusions:** applying the plan, advanced anchors, glow/bloom.
- **Tests:** valid ghost, orange overlaps, red blocking failure, palette colors,
  bounds, counts, unchanged plan reuse, exact position equality with future
  operation data.
- **Acceptance:** preview and application consume the same immutable plan; no
  recalculation in renderer or workspace.
- **Risks:** large preview throughput; state/color mismatch.
- **Size:** Medium.

### STAMP-15 — Atomic Place Voxel Stamp operation

- **Goal:** apply one validated placement plan as one document command and one
  Undo/Redo entry.
- **Probable files:** new
  `Placement/PlaceVoxelStampOperation.h/.cpp`;
  minimal history integration; `tests/Editor/PlaceVoxelStampOperationTests.cpp`.
- **Public API:** `PreparePlaceVoxelStampOperation`,
  `ExecutePlaceVoxelStampOperation`.
- **Dependencies:** STAMP-11, STAMP-13.
- **Explicit exclusions:** repeated placement session and UI.
- **Tests:** add, overwrite, skip occupied, rejected collision, stale
  generation/revision, palette mutation, rollback, Undo/Redo exactness, one
  revision/rebuild/history item, memory limit refusal.
- **Acceptance:** no partial mutation is observable; Redo uses stored operation
  data and never replans.
- **Risks:** atomicity around palette changes; history memory.
- **Size:** Medium.

### STAMP-16 — Placement session and continuous placement

- **Goal:** own the active Stamp, transform, ordinal, preview and repeated
  click-to-place lifecycle until Esc.
- **Probable files:** new `Placement/StampPlacementSession.h/.cpp`;
  minimal `EditorWorkspace` and input routing;
  `tests/Editor/StampPlacementSessionTests.cpp`.
- **Public API:** `Begin`, `UpdateTarget`, `PlaceOnce`, `Cancel`,
  `SelectAsset`, session state/query methods.
- **Dependencies:** STAMP-10, STAMP-14, STAMP-15.
- **Explicit exclusions:** Auto Repeat, variant groups, persisted transform.
- **Tests:** begin/update/place multiple/cancel, ordinal increments only on
  successful placement, document switch cancellation, missing asset, no
  persistence across restart.
- **Acceptance:** one asset can be placed multiple times; Esc clears preview;
  every click is one atomic Undo entry.
- **Risks:** input ownership conflicts with Smart Tool/Selection/Transform.
- **Size:** Medium.

### STAMP-17 — Quick rotation and mirror

- **Goal:** add session-local quick rotation/mirror controls using planner input.
- **Probable files:** modifications to `StampPlacementSession`,
  `EditorInputService` and presentation model; additions to session/planner
  tests.
- **Public API:** session `Rotate90(axis)`, `ToggleMirror(axis)`,
  `ResetTransform`.
- **Dependencies:** STAMP-13, STAMP-16.
- **Explicit exclusions:** scale, custom pivot, restart persistence.
- **Tests:** X/Y/Z rotations, mirror, combined transform, asset switch loads its
  saved transform or neutral state, Esc ends state, restart neutral, preview
  equals placement.
- **Acceptance:** transform persists only for the active placement session and
  never changes stored geometry.
- **Risks:** shortcut conflicts; integer pivot transform errors.
- **Size:** Small.

### STAMP-18 — Forge Library integration

- **Goal:** expose Project/My creations in the existing right-side REUSE area
  with search, category, favorites/recent views and placement activation.
- **Probable files:** new `ForgeLibraryViewModel.h/.cpp`,
  `ForgeLibraryPanel.h/.cpp`; minimal `EditorWorkspace` docking/routing;
  existing `AssetBrowser` integration only where required;
  `tests/Editor/ForgeLibraryViewModelTests.cpp`.
- **Public API:** scope/category/search query, card presentation, selection
  command, placement activation command.
- **Dependencies:** STAMP-07, STAMP-09, STAMP-10, STAMP-16.
- **Explicit exclusions:** marketplace, cloud, Asset Packs, advanced cards,
  renderer changes.
- **Tests:** Assets/Creations/Brushes/Favorites/Recent organization, Project/My
  scope, search/tags, missing asset presentation, asset selection begins
  placement, Brushes are not creations.
- **Acceptance:** the MVP path “find creation → live preview → place” is
  available with Assets as the primary right-side workspace.
- **Risks:** conflating generic Asset Browser files and Forge catalogue entries.
- **Size:** Large.

### STAMP-19 — Smart Variant domain and deterministic resolver

- **Goal:** implement fixed, sequential, random and weighted variant resolution
  independent of placement.
- **Probable files:** new `Variants/StampVariantGroup.h/.cpp`,
  `StampVariantResolver.h/.cpp`; `tests/Editor/StampVariantTests.cpp`.
- **Public API:** `ResolveVariant(group, sessionSeed, ordinal)`,
  `RenewSessionSeed`, typed resolution report.
- **Dependencies:** STAMP-01 and library references.
- **Explicit exclusions:** UI and document mutation.
- **Tests:** fixed, sequential, random reproducibility, weighted distribution
  boundaries, missing/disabled/zero/invalid weights ignored and normalized,
  one remaining variant, zero valid variants blocked, same tuple → same UUID.
- **Acceptance:** resolver returns an exact UUID or an explicit failure; never an
  arbitrary fallback.
- **Risks:** cross-platform hash instability; floating-point weighted selection.
- **Size:** Medium.

### STAMP-20 — Variant-aware placement and Undo/Redo identity

- **Goal:** connect the deterministic resolver to placement while storing exact
  chosen UUID and seed facts in the operation/session.
- **Probable files:** modifications to `StampPlacementSession`,
  `PlaceVoxelStampOperation`, history metadata; additions to variant/operation
  tests.
- **Public API:** variant fields in immutable placement/operation metadata.
- **Dependencies:** STAMP-15, STAMP-16, STAMP-19.
- **Explicit exclusions:** variant-authoring UI beyond minimal test hooks.
- **Tests:** successive ordinal choices, failed placement does not consume
  ordinal, Undo/Redo exact UUID, no reroll, manual seed renewal, missing chosen
  UUID on Redo yields explicit safe failure policy.
- **Acceptance:** Redo never invokes `ResolveVariant`.
- **Risks:** operation metadata lifetime; asset deleted between Undo and Redo.
- **Size:** Medium.

### STAMP-21 — Smart Placement minimal assist

- **Goal:** propose orientation, pivot and surface alignment without changing
  geometry, forcing orientation or blocking an otherwise valid placement.
- **Probable files:** new
  `SmartPlacement/StampSmartPlacementService.h/.cpp`;
  integration into `StampPlacementSession`;
  `tests/Editor/StampSmartPlacementTests.cpp`.
- **Public API:** `SuggestPlacement(context)`, enable/temporary-bypass flags.
- **Dependencies:** STAMP-05, STAMP-13, STAMP-16.
- **Explicit exclusions:** AI, collision constraints, anchors, environment
  deformation.
- **Tests:** default enabled, deterministic suggestions, global disable,
  temporary bypass, explicit user transform wins, suggestion cannot convert
  allowed placement into refusal.
- **Acceptance:** service output is advisory data consumed by the session.
- **Risks:** hidden “smart” behavior feeling mandatory; context ambiguity.
- **Size:** Medium.

### STAMP-22 — Robustness and performance hardening

- **Goal:** meet resource, latency, cache and corruption budgets under realistic
  sparse and dense workloads.
- **Probable files:** targeted modifications in format, cache, catalogue,
  planner and preview; `tests/Editor/StampPerformanceTests.cpp`.
- **Public API:** metrics only where needed; no product features.
- **Dependencies:** STAMP-01 through STAMP-21 as applicable.
- **Explicit exclusions:** changing format or UX to hide failures.
- **Tests/benchmarks:** see Section 7.
- **Acceptance:** no hard-limit allocation, no per-frame catalogue scan,
  bounded cache, stable idle preview, documented benchmark results.
- **Risks:** premature optimization; environment-sensitive thresholds.
- **Size:** Medium.

### STAMP-23 — End-to-end smoke, documentation and release gate

- **Goal:** register focused smoke scenarios, user help and failure diagnostics,
  then validate the full V1 implementation.
- **Probable files:** `tests/Editor/VoxelStampSmokeTests.cpp`,
  `tests/CMakeLists.txt`, `editor/src/main.cpp`, user documentation under
  `docs/`, minimal contextual help text.
- **Public API:** `--voxel-stamp-smoke-test` and separate focused smoke modes if
  runtime isolation requires them.
- **Dependencies:** all MVP lots; variant/smart-placement smoke only when those
  lots are included in the release.
- **Explicit exclusions:** new features.
- **Tests:** full MVP scenario, recovery/corruption, Project/My scope, palette
  Undo/Redo, continuous placement, restart behavior.
- **Acceptance:** all gates in Section 10 pass and Tony validates the workflow.
- **Risks:** an oversized smoke hiding which subsystem failed.
- **Size:** Small.

---

## 5. MVP definition

### 5.1 Exact MVP user path

```text
Select existing voxels
    ↓
Save Selection As…
    ↓
Creation appears in Project Forge Library
    ↓
Find it by category/search
    ↓
Select it
    ↓
Exact live preview follows the target
    ↓
Click to place
    ↓
Click again to place another copy
    ↓
Esc ends placement
    ↓
Undo/Redo restores exact palette and voxel states
```

The MVP is complete after STAMP-18, provided STAMP-01 through STAMP-18 and all
MVP gates pass.

### 5.2 MVP includes

- capture from the current selection;
- deterministic Auto Pivot and explicit presets required by V2;
- Project Library source asset;
- `.vfstamp` read/write/validation;
- rebuildable local catalogue;
- Forge Library discovery/search;
- lazy CPU loading;
- exact live voxel preview;
- default overwrite overlap with orange preview;
- Reject and SkipOccupied policy support at domain/planner level;
- rotation/mirror for the active placement session;
- multiple placements until Esc;
- atomic palette-plus-voxel Undo/Redo;
- soft/hard resource limits;
- scale `1:1` only.

### 5.3 Explicitly outside MVP

- anchors;
- Auto Repeat;
- any scale other than 1;
- custom/advanced pivots;
- advanced Smart Placement;
- Smart Variants, unless promoted after MVP validation;
- AI;
- Prefabs;
- cloud;
- marketplace;
- community sharing;
- Asset Packs;
- procedural generation;
- SQLite;
- cross-device My Library synchronization.

My Library storage may be implemented before the MVP gate because it is small
and architectural, but the MVP remains valid with Project Library as the only
user-visible creation scope.

---

## 6. Detailed test strategy

### 6.1 Unit tests

#### Domain and limits

- normalized local voxel coordinates;
- compact palette references;
- bounds and pivot validity;
- UUID and content-hash stability;
- scale exactly 1;
- configurable soft thresholds;
- hard rejection before allocation;
- checked multiplication/addition and decoded-size overflow.

#### Format

- minimum valid file;
- deterministic canonical serialization;
- round trip;
- all required chunks;
- unknown optional chunk;
- unknown required chunk;
- duplicate required chunk;
- truncated header/chunk/payload;
- invalid chunk size;
- hash mismatch;
- malformed UTF-8/metadata limits;
- decompression bomb protection if compression is later enabled;
- unsupported major version and tolerated minor version.

#### Capture and pivot

- sparse selection;
- holes preserved;
- stale generation/revision;
- missing selected voxel;
- local coordinate normalization;
- deterministic Auto Pivot for every approved context;
- explicit preset override;
- even/odd bounds and negative local offsets if permitted.

#### Libraries and catalogue

- path confinement;
- symlink/reparse escape;
- atomic install/rollback;
- unique name;
- Project/My scope separation;
- no absolute profile path in project data;
- catalogue add/update/remove;
- corrupt/missing catalogue rebuild;
- stable search/category/tag ordering;
- source assets remain authoritative.

#### Planner

- every rotation/mirror combination;
- 1:1 scale acceptance and non-1 rejection;
- bounds and integer overflow;
- palette exact reuse;
- palette new-slot mapping;
- palette exhaustion;
- default overwrite;
- Reject;
- SkipOccupied;
- orange overlap state;
- red blocking state;
- statistics invariants.

#### History

- combined palette/voxel forward apply;
- combined backward apply;
- rollback on any sub-step;
- memory estimation;
- saved-state tracking;
- exact one revision/rebuild;
- exact chosen variant metadata.

#### Variants

- fixed/sequential/random/weighted;
- deterministic tuple;
- manual seed renewal;
- invalid entries ignored;
- zero valid entry failure;
- Undo/Redo no reroll.

### 6.2 Integration tests

- SelectionService + capture + serializer;
- serializer + project repository + catalogue rebuild;
- catalogue query + cache load;
- planner + common preview;
- planner + compound command + mesh sync;
- placement session + input lifecycle;
- asset switch resets/loads session-local transform as specified;
- project switch invalidates caches and placement;
- missing/corrupt source during an active session;
- Project Library data remains portable after project-root relocation;
- My Library references never serialize an absolute path into project files.

### 6.3 Regression tests

The full existing CTest suite must pass after every lot. Special attention:

- selection;
- transform preview and operations;
- voxel edit history;
- pencil/Smart Brush;
- Asset Browser;
- import metadata/thumbnails;
- document save and mesh sync;
- project session and layout stability;
- Save-on-exit.

### 6.4 Smoke tests

At minimum:

1. `--voxel-stamp-smoke-test`
   - create/open project and model;
   - select voxels;
   - save selection;
   - rebuild/search catalogue;
   - begin preview;
   - place twice;
   - Esc;
   - Undo twice;
   - Redo twice;
   - save/reopen;
   - cleanup isolated temp profile.
2. `--voxel-stamp-corruption-smoke-test`
   - malformed source is reported;
   - catalogue can rebuild;
   - no document mutation.
3. `--voxel-stamp-library-smoke-test`
   - Project/My scopes;
   - no profile path leakage;
   - missing source presentation.
4. `--voxel-stamp-variant-smoke-test`, only when STAMP-20 is in scope.

Smoke tests must never read or write Tony’s real profile or personal assets.

### 6.5 Manual Tony validation

MVP:

1. create a recognizable multi-color selection with holes;
2. choose **Save Selection As…**;
3. verify the creation appears in Forge Library;
4. search for it;
5. activate it;
6. verify exact live preview and pivot;
7. place on empty space;
8. place overlapping an existing object and verify orange overlap;
9. verify only overlapped cells are replaced;
10. place multiple copies;
11. press Esc and verify preview disappears;
12. Undo/Redo each placement;
13. save, close and reopen;
14. relocate a copied project and verify Project Library portability.

---

## 7. Performance plan and benchmark budgets

Benchmarks must report hardware, build type, document density, Stamp voxel
count, palette size and cold/warm cache state. Debug measurements detect
pathological behavior; Release measurements validate user budgets.

### 7.1 Proposed V1 budgets

| Operation | Dataset | Target |
|---|---|---:|
| Parse warm-cache lookup | unchanged Stamp | `< 0.2 ms` median, no file read |
| Parse cold small Stamp | 4,096 voxels | `< 5 ms` median |
| Parse cold soft-limit Stamp | 262,144 voxels | `< 80 ms` median |
| Catalogue load | 1,000 entries | `< 40 ms` |
| Catalogue rebuild | 1,000 valid small assets | `< 2 s`, progress-capable |
| Search/filter | 10,000 entries | `< 8 ms` per query update |
| Placement plan small | 4,096 voxels | `< 2 ms` median |
| Placement plan soft limit | 262,144 voxels | `< 16 ms` preferred, `< 33 ms` hard interaction target |
| Unchanged preview frame | any cached plan | no plan rebuild, no allocation proportional to voxel count |
| Preview update | 65,536 voxels | stable interactive frame pacing near 60 FPS on reference hardware |
| Commit | 262,144 changes | bounded by one command, one revision and one mesh rebuild |
| Undo/Redo | same operation | no parse/replan/reroll |

These are validation targets, not format promises. Resource-limit defaults may
change only after recorded benchmarks and architecture review.

### 7.2 Required measurements

- allocations during parse, plan, unchanged preview and commit;
- cache hit/miss and retained bytes;
- sparse versus dense destination document;
- 0%, 25%, 100% overlap;
- palette reuse versus palette insertion/exhaustion;
- cold versus warm catalogue;
- catalogue rebuild with corrupt and missing assets;
- preview rebuild count while the target cell is unchanged;
- time to reject hard-limit input before allocation;
- Undo memory estimate versus actual payload size.

### 7.3 Performance acceptance rules

- no scan of all library files per UI frame;
- no full parse per preview frame;
- no plan rebuild when target, transform, policy, document revision and asset
  identity are unchanged;
- no iteration across empty destination volume when sparse document iteration
  or direct coordinate lookup is sufficient;
- no GPU resource in catalogue/domain caches;
- no silent lowering of fidelity to pass a benchmark;
- large-preview simplification, if required, must keep global bounds and exact
  placement data.

---

## 8. Git and change-management rules per lot

Each lot is a separate local commit after technical and, when applicable,
visual validation.

### 8.1 Before each lot

1. Read `AGENTS.md` and this plan.
2. Verify branch and HEAD.
3. Run `git status --short --branch`, `git diff --stat` and
   `git diff --check`.
4. Identify and preserve all pre-existing changes.
5. In particular, preserve the current Brush Profile work; do not restore,
   stage or mix it with Stamp lots.
6. List the exact planned files before editing.

### 8.2 During each lot

- no `git reset --hard`;
- no `git clean`;
- no broad restore/checkout;
- no unrelated formatting;
- no `git add .` or `git add -A`;
- use explicit file lists;
- keep `EditorWorkspace` changes limited to routing;
- stop if a blocking dependency requires a wider architecture decision.

### 8.3 Validation for each lot

Unless the lot is documentation-only:

```powershell
cmake --preset windows-debug
cmake --build --preset build-windows-debug
ctest --test-dir build/windows-debug --output-on-failure
git diff --check
```

Run every registered smoke test when the editor integration changes. Run the
focused new test executable before the full suite.

Before commit:

```powershell
git diff --cached --name-status
git diff --cached --stat
git diff --cached --check
git status --short
```

### 8.4 Commit and publication

- one local commit per validated lot;
- message names only that lot;
- no push before Tony’s validation;
- no amend/rebase/squash unless explicitly authorized;
- no Stamp commit may contain Brush Profiles, personal assets, build outputs,
  captures, caches, layouts or temporary files.

---

## 9. Quality gates

### Gate G0 — Plan approval

- This plan reviewed against VF-0250.
- No blocking ambiguity.
- Lot order and MVP boundary approved.

### Gate G1 — Domain and format

Requires STAMP-01 through STAMP-04:

- deterministic canonical round trip;
- malformed-input matrix green;
- hard limits enforced before allocation;
- format/domain independent from UI/rendering.

### Gate G2 — Capture and durable Project Library

Requires STAMP-05 through STAMP-08:

- deterministic capture/Auto Pivot;
- safe atomic source installation;
- catalogue rebuild from assets;
- Save Selection As end-to-end without placement.

### Gate G3 — Placement foundations

Requires STAMP-10 through STAMP-13:

- bounded cache;
- compound document edit proven atomic;
- common preview contract proven;
- planner pure and deterministic.

### Gate G4 — MVP technical complete

Requires STAMP-14 through STAMP-18:

- exact live preview;
- atomic placement;
- continuous placement and Esc;
- quick rotation/mirror;
- Forge Library discovery;
- full CTest and smoke suite green.

### Gate G5 — MVP product validation

- Tony completes Section 6.5;
- project relocation succeeds;
- no unintended UX regression;
- no profile or personal asset access by tests;
- performance budgets accepted or deviations documented.

### Gate G6 — Smart Variants

Requires STAMP-19 and STAMP-20:

- deterministic resolver;
- exact UUID stored;
- no reroll on Undo/Redo;
- zero-valid weighted group blocks clearly.

### Gate G7 — Smart Placement

Requires STAMP-21:

- enabled by default;
- advisory only;
- deterministic;
- user override and disable paths validated.

### Gate G8 — Release hardening

Requires STAMP-22 and STAMP-23:

- benchmark record;
- corruption/recovery record;
- complete smoke suite;
- documentation;
- clean repository scope;
- no residual editor process or temporary artifact.

---

## 10. Recommended implementation order

| Order | Lot | Short name | Depends on | MVP | Size | Gate |
|---:|---|---|---|:---:|:---:|---|
| 1 | STAMP-01 | Domain Core + limits | — | Yes | M | G1 |
| 2 | STAMP-02 | Container reader | 01 | Yes | M | G1 |
| 3 | STAMP-03 | Deterministic writer | 01–02 | Yes | M | G1 |
| 4 | STAMP-04 | Semantic validation | 01–03 | Yes | S | G1 |
| 5 | STAMP-05 | Capture + Auto Pivot | 01, 04 | Yes | M | G2 |
| 6 | STAMP-06 | Project Library repository | 02–04 | Yes | M | G2 |
| 7 | STAMP-07 | JSON catalogue + rebuild | 06 | Yes | M | G2 |
| 8 | STAMP-08 | Save Selection As workflow | 05–07 | Yes | M | G2 |
| 9 | STAMP-09 | My Library | 06–07 | Optional for MVP UI | S | G2 |
| 10 | STAMP-10 | Lazy CPU cache | 04, 06, 09 | Yes | S | G3 |
| 11 | STAMP-11 | Compound document edit | existing history | Yes | L | G3 |
| 12 | STAMP-12 | Common preview contract | existing previews | Yes | M | G3 |
| 13 | STAMP-13 | Palette mapper + planner | 01, 04, 10–11 | Yes | L | G3 |
| 14 | STAMP-14 | Exact live preview | 12–13 | Yes | M | G4 |
| 15 | STAMP-15 | Atomic place operation | 11, 13 | Yes | M | G4 |
| 16 | STAMP-16 | Placement session | 10, 14–15 | Yes | M | G4 |
| 17 | STAMP-17 | Quick rotation/mirror | 13, 16 | Yes | S | G4 |
| 18 | STAMP-18 | Forge Library integration | 07, 09–10, 16 | Yes | L | G4/G5 |
| 19 | STAMP-19 | Variant resolver | 01, library refs | No | M | G6 |
| 20 | STAMP-20 | Variant-aware placement | 15–16, 19 | No | M | G6 |
| 21 | STAMP-21 | Smart Placement assist | 05, 13, 16 | No | M | G7 |
| 22 | STAMP-22 | Performance hardening | implemented scopes | Yes before release | M | G8 |
| 23 | STAMP-23 | Smokes/docs/release gate | release scope | Yes | S | G8 |

Parallel work is permitted only when file ownership does not overlap. In
particular:

- STAMP-11 and STAMP-12 may proceed independently after their interfaces are
  agreed;
- STAMP-09 may proceed in parallel with STAMP-11/12;
- STAMP-19 may begin after stable library references, but must not enter MVP
  commits;
- no two concurrent lots may edit `EditorWorkspace.cpp`,
  `editor/CMakeLists.txt` or `tests/CMakeLists.txt` without coordination.

---

## 11. Major implementation risks

1. **Atomic palette history:** the largest correctness and regression risk.
2. **EditorWorkspace growth:** Stamp domain logic must not be routed into the
   existing large workspace class.
3. **Preview fragmentation:** adding a Stamp-only rendering path would create
   long-term divergence from brush/transform preview.
4. **Catalogue authority inversion:** derived JSON must never replace
   `.vfstamp` assets as source of truth.
5. **Path portability:** My Library absolute paths must never leak into project
   data.
6. **Untrusted format input:** sizes and chunk arithmetic must be checked before
   allocation.
7. **Palette exhaustion:** must be an explicit plan failure or policy result,
   never a silent nearest-color substitution unless separately approved.
8. **Redo determinism:** no reparse, replan or variant reroll during Redo.
9. **Per-frame work:** catalogue scans, file reads and full replans are forbidden
   on unchanged frames.
10. **Scope contamination:** existing uncommitted Brush Profile work must remain
    separate throughout all lots.

---

## 12. Remaining non-blocking implementation choices

The architecture has no blocking ambiguity. The following choices may be made
inside their designated lot and documented in that lot’s report:

- exact namespace subdivision below `VoxelForge::Editor`;
- exact JSON field spelling for the derived V1 catalogue;
- exact card thumbnail generation strategy, provided the catalogue remains
  renderer-independent;
- exact shortcut keys for rotation/mirror after conflict audit;
- exact error enum names;
- exact bounded-cache eviction policy;
- whether STAMP-09 is visible in the first MVP UI or enabled immediately after
  the Project Library MVP;
- final soft-limit defaults after STAMP-22 benchmarks, within the central
  configurable policy.

None of these choices may change an approved VF-0250 behavior.

---

## 13. Definition of implementation complete

Voxel Stamps V1 is complete only when:

- STAMP-01 through STAMP-18 and STAMP-22/23 are validated;
- the MVP path in Section 5 works without hidden preparation;
- format, capture, storage, catalogue, planner and operation code are independent
  from ImGui and the renderer;
- Project Library is portable;
- My Library cannot leak absolute paths into projects;
- exact preview and exact applied voxels come from the same plan;
- one placement is one atomic Undo/Redo operation including palette changes;
- overlap is allowed by default and shown orange;
- invalid/blocking states are shown red and do not mutate the document;
- scale other than 1 is rejected;
- hard limits are enforced before allocation;
- the full existing test/smoke suite remains green;
- performance and corruption gates pass;
- Tony validates the workflow;
- commits remain separated by lot;
- no push occurs without explicit authorization.
