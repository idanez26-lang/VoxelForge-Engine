# VF-0252 — Voxel Stamps Performance Baseline V1

## Baseline status

- **Document type:** measured performance baseline
- **Baseline version:** V1
- **Architecture source:** `VF-0250-Voxel-Stamps-Architecture-V2.md`
- **Implementation plan:** `VF-0251-Voxel-Stamps-Implementation-Plan-V1.md`, STAMP-22
- **Measurement date:** 2026-08-04
- **Target branch:** `feature/imgui`
- **Primary acceptance build:** Release, x64, MSVC

---

## 1. Purpose

This document records the STAMP-22 resource, latency and cache baseline. The
Release measurements are the V1 acceptance values. Debug measurements are
diagnostic only: they make pathological work visible but are not compared to
the interaction budgets from VF-0251.

The benchmark is implemented by
`tests/Editor/StampPerformanceTests.cpp` and is registered in CTest as
`VoxelForge.Editor.StampPerformance` with the `Performance` label.

## 2. Reference environment

| Component | Value |
|---|---|
| CPU | AMD Ryzen 7 3700X, 8 cores / 16 logical processors |
| Memory | 31.9 GiB physical memory |
| Operating system | Microsoft Windows 10.0.26200, x64 |
| Compiler | MSVC 19.51.36231 (`cl` 14.51.36231 toolset) |
| Generator | Ninja |
| Release configuration | Optimized, `NDEBUG`, `/O2` |
| Debug configuration | Unoptimized diagnostic build |

Measurements are wall-clock timings on a developer workstation. Each median
uses the sample count encoded by the test; large 262,144-voxel operations are
single measured runs to keep the regular test suite bounded.

## 3. Release acceptance results

| Operation | Measured dataset | VF-0251 target | Result | Status |
|---|---:|---:|---:|---|
| Warm asset-cache lookup | unchanged Stamp, 101 samples | `< 0.2 ms` median, no file read | `0.001 ms` median | Pass |
| Cold decode | 4,096 voxels, 9 samples | `< 5 ms` median | `0.611 ms` median | Pass |
| Cold decode | 262,144 voxels | `< 80 ms` | `37.460 ms` | Pass |
| Catalogue load and search-index construction | 10,000 entries | `< 40 ms` for 1,000 entries | `29.660 ms` | Pass on a 10x dataset |
| In-memory catalogue search | 10,000 entries, 51 samples | `< 8 ms` per update | `0.278 ms` median | Pass |
| Placement plan, sparse | 4,096 voxels, 9 samples | `< 2 ms` median | `0.214 ms` median | Pass |
| Placement plan, 25% overlap | 4,096 voxels, 9 samples | `< 2 ms` median | `0.230 ms` median | Pass |
| Placement plan, 100% overlap | 4,096 voxels, 9 samples | `< 2 ms` median | `0.236 ms` median | Pass |
| Placement plan, sparse | 262,144 voxels | `< 16 ms` preferred; `< 33 ms` hard | `12.840 ms` | Pass preferred target |
| Preview materialization | 262,144 planned voxels | 65,536 near 60 FPS | `7.716 ms` | Pass workload proxy; see limitations |
| Unchanged preview requests | 1,000 requests | no rebuild or proportional allocation | `0.384 ms` total | Pass structurally |

## 4. Structural resource and cache assertions

Timing alone is not used to infer the absence of work. The benchmark also
asserts the following deterministic counters and limits:

- a canonical VOX0 payload above its hard voxel limit is rejected before chunk
  payload copying or decoded voxel allocation;
- the placement planner rejects a 4,096-voxel input against a 4,095 hard limit
  before allocating its planned-voxel vector;
- 101 warm cache reads produce exactly one repository load, 101 cache hits and
  one retained entry, while retained bytes remain within the configured byte
  budget;
- 52 catalogue queries produce one catalogue-store load and 51 cache hits;
- repeated queries produce zero source inventory scans, source reads,
  repository enumerations or catalogue rebuilds;
- the in-memory search index contains exactly 10,000 entries and reports a
  non-zero bounded retained size;
- 1,000 unchanged placement-session requests produce 1,000 plan-cache hits,
  zero planner builds, zero preview builds, zero preview changes and no preview
  revision change;
- one document revision change invalidates the cached plan and produces exactly
  one planner build and one preview build.

Malformed-container, checksum, interrupted-catalogue-write and catalogue
recovery behavior remain covered by the dedicated format and catalogue tests.

## 5. Debug diagnostic reference

| Operation | Result |
|---|---:|
| Cold decode, 4,096 voxels | `2.068 ms` median |
| Cold decode, 262,144 voxels | `124.536 ms` |
| Warm asset-cache lookup | `0.007 ms` median |
| Catalogue load and index, 10,000 entries | `328.284 ms` |
| Search, 10,000 entries | `1.026 ms` median |
| Placement, 4,096 sparse / 25% / 100% overlap | `2.921 / 3.125 / 3.409 ms` median |
| Placement, 262,144 sparse voxels | `182.887 ms` |
| Preview, 262,144 planned voxels | `83.382 ms` |
| 1,000 unchanged preview requests | `5.832 ms` total |

## 6. Known limits of this baseline

- Catalogue rebuild of 1,000 on-disk assets is covered for correctness and
  recovery by dedicated tests but is not timed by this in-memory benchmark.
- The preview result measures one exact 262,144-voxel materialization. It is a
  stronger workload-size proxy than the 65,536-voxel target and completes below
  a 16.67 ms frame, but it is not a long-duration frame-pacing capture.
- Commit, mesh rebuild and Undo/Redo timing are integration concerns and are not
  measured by this domain benchmark. Their atomicity remains covered by the
  placement and history tests.
- Structural counters prove that proportional planner and preview work is
  skipped while idle. They do not replace a heap-profiler trace for every
  constant-size library allocation.

These explicit gaps are documented deviations, not inferred passes. They may be
promoted to dedicated integration benchmarks before the final V1 release gate
if profiling or real-project testing shows a regression risk.

## 7. Reproduction

Build and run the benchmark in both configurations:

```powershell
cmake --build build/windows-release --target VoxelForgeEditorStampPerformanceTests
.\build\windows-release\tests\VoxelForgeEditorStampPerformanceTests.exe

cmake --build build/windows-debug --target VoxelForgeEditorStampPerformanceTests
.\build\windows-debug\tests\VoxelForgeEditorStampPerformanceTests.exe
```

Every timing is emitted as `STAMP22_METRIC name=value ms`. Correctness and
structural-budget failures return a non-zero process exit code.
