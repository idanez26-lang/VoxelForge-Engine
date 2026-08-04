# VF-0253 — Voxel Stamps V1 Release Gate

## Gate status

- **Document type:** STAMP-23 validation and release record
- **Validation date:** 2026-08-04
- **Architecture:** `VF-0250-Voxel-Stamps-Architecture-V2.md`
- **Implementation plan:** `VF-0251-Voxel-Stamps-Implementation-Plan-V1.md`
- **Performance record:** `VF-0252-Voxel-Stamps-Performance-Baseline-V1.md`
- **Validated branch:** `feature/imgui`
- **Published baseline before STAMP-23:** `67b3ff3`
- **Technical verdict:** Pass
- **Product verdict:** Awaiting Tony's manual Section 6.5 validation

---

## 1. STAMP-23 scope

STAMP-23 adds no product feature. It closes the V1 release surface with:

- a headless aggregate release smoke;
- five independently runnable diagnostic modes;
- an exact MVP scenario crossing capture, durable storage, catalogue, cache,
  Forge Library, preview, continuous placement, history and restart;
- corruption/recovery, Project/My scope, Smart Variant and Smart Placement
  release scenarios;
- contextual Forge Library help;
- a V1 user and failure-diagnostic guide;
- repository, process and temporary-artifact checks.

All filesystem-backed scenarios inject disposable project and local-profile
roots. They return before GPU/application initialization and remove the roots
before the process exits.

## 2. Registered release smokes

| CTest | Editor diagnostic switch | Covered release behavior | Result |
|---|---|---|---|
| `VoxelForge.Editor.VoxelStampReleaseSmoke` | aggregate test executable | all five focused scenarios in one process | Pass |
| `VoxelForge.Editor.VoxelStampMvpSmoke` | `--voxel-stamp-smoke-test` | capture, Project Library install/search, exact preview, two placements, Esc, two Undo/Redo, save/reopen, fresh session, relocated project | Pass |
| `VoxelForge.Editor.VoxelStampCorruptionSmoke` | `--voxel-stamp-corruption-smoke-test` | corrupt catalogue rejection, rebuild, malformed source diagnostic, no mutation | Pass |
| `VoxelForge.Editor.VoxelStampLibrarySmoke` | `--voxel-stamp-library-smoke-test` | Project/My scopes, portable references, profile-path isolation, missing-source presentation | Pass |
| `VoxelForge.Editor.VoxelStampVariantSmoke` | `--voxel-stamp-variant-smoke-test` | sequential exact UUIDs, stored metadata, source-independent Redo without reroll | Pass |
| `VoxelForge.Editor.VoxelStampSmartPlacementSmoke` | `--voxel-stamp-smart-placement-smoke-test` | default advisory assist, deterministic surface alignment, user rotation, bypass and disable | Pass |

The six tests carry both `Smoke` and `StampRelease` labels and have a 30-second
timeout. The measured aggregate runtime was below one second on the reference
workstation.

## 3. End-to-end MVP record

The headless MVP gate completed the following exact sequence:

1. constructed a multicolour voxel source with a non-solid selection;
2. captured it through `SaveSelectionAsStampWorkflow`;
3. installed the canonical `.vfstamp` in the Project Library;
4. found the derived catalogue entry by text search;
5. selected it through `ForgeLibraryViewModel` and activated the shared exact
   placement preview;
6. moved the preview and committed two continuous placements;
7. cancelled the active preview as the Esc-equivalent without mutation;
8. executed two Undo and two Redo operations atomically;
9. serialized and reopened the voxel document with equivalent voxels/palette;
10. opened the Stamp through fresh repository, catalogue and cache instances;
11. copied the project to a new root and resolved the same portable Stamp and
    catalogue there;
12. verified that the relocated catalogue contained no original absolute root;
13. removed the complete isolated fixture.

## 4. Corruption and recovery record

The focused corruption gate established a valid source/catalogue control, then:

- wrote a malformed `.vfstamp` beside the valid source;
- replaced the derived JSON catalogue with invalid JSON;
- confirmed normal catalogue loading returned `StampCatalogError::Invalid`;
- rebuilt only from source inventory;
- retained the valid source as one catalogue entry;
- reported the malformed source as a deterministic diagnostic;
- kept the malformed source on disk for recovery rather than deleting it;
- preserved the active document revision and voxel state;
- removed the isolated fixture.

Dedicated format, checksum, transaction rollback and catalogue tests continue
to provide the exhaustive malformed-input matrix behind this smoke record.

## 5. Validation results

| Validation | Result |
|---|---|
| Debug full build | Pass |
| Release editor and STAMP-23 aggregate build | Pass |
| New STAMP-23 release tests (Debug and Release) | `6 / 6` pass in each configuration |
| Focused Voxel Stamp regression set | `36 / 36` pass |
| Full repository CTest | `195 / 195` pass |
| Performance benchmark | Pass; recorded by VF-0252 |
| `git diff --check` | Pass |
| Local/remote baseline before this uncommitted lot | `0 / 0` commits divergent |
| Residual `VoxelForgeEditor` process | None |
| Residual `VoxelForgeStamp23-*` temporary root | None |
| Unrelated source, profile asset, build output or layout in local diff | None |

The usual optional configure notices remain unchanged: PkgConfig and LibUSB are
not present. The existing MSVC `getenv` deprecation warning in `main.cpp` is
unrelated to Voxel Stamps and does not block the build.

## 6. Quality-gate matrix

| Gate | Evidence | Status |
|---|---|---|
| G0 — Plan approval | VF-0250 approved; VF-0251 has no blocking ambiguity | Pass |
| G1 — Domain and format | canonical/malformed/limit suites plus full CTest | Pass |
| G2 — Capture and durable Project Library | workflow, real filesystem source/catalogue and relocation smoke | Pass |
| G3 — Placement foundations | bounded cache, palette/history atomicity, pure planner tests | Pass |
| G4 — MVP technical complete | exact preview, continuous placement, Esc, transforms, Forge Library and smokes | Pass |
| G5 — MVP product validation | automated relocation/performance pass; real UI/visual checklist requires Tony | Pending manual validation |
| G6 — Smart Variants | deterministic resolver and exact source-independent Redo smoke | Pass |
| G7 — Smart Placement | default advisory behavior, override, bypass and disable smoke | Pass |
| G8 — Release hardening | VF-0252 benchmark, corruption record, complete smoke suite, docs and clean runtime scope | Technical pass |

## 7. Required manual sign-off

Automation cannot judge the artist-facing visual quality. Tony must still run
the Section 6.5 workflow from the user guide and confirm:

- the Save Selection As dialog is understandable;
- the saved creation appears and searches correctly in the real Forge Library;
- pivot and exact preview look correct;
- valid, orange-overlap and red-blocked states are visually clear;
- only overlapped cells are replaced;
- repeated placement, Esc and Undo/Redo feel correct;
- save/reopen and a copied/relocated project work in the real UI;
- no unintended UX regression is visible.

Until this sign-off, the correct statement is **“Voxel Stamps V1 technically
ready for product validation”**, not “product release approved.”

## 8. Reproduction

```powershell
cmake --build build/windows-debug
ctest --test-dir build/windows-debug -L StampRelease --output-on-failure
ctest --test-dir build/windows-debug --output-on-failure
```

Focused diagnostics may also be run directly with the five switches listed in
the user guide. A successful run prints the scenario name and `smoke passed`;
failure prints the exact isolated subsystem and returns a non-zero exit code.
