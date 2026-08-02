# VoxelForge developer benchmarks

These executables are profiling fixtures. They are deliberately not registered
with CTest and do not define product pass/fail criteria.

## VF-0262 incremental edit baseline (lot 262-0)

CPU-only; no GPU or window required. Compares the full document mesh build
(today's per-commit cost) with a 32^3 region build per document size:

```powershell
cmake --preset windows-release -DVF_BUILD_BENCHMARKS=ON
cmake --build --preset build-windows-release --target VoxelForgeIncrementalEditBenchmark
.\build\windows-release\benchmarks\VoxelForgeIncrementalEditBenchmark.exe
```

CSV columns: `voxels,full_build_ms,region32_build_ms,ratio`. The residual
region cost exposes the collection traversal that stays O(document) until
lots 262-2/262-3 introduce regional iteration.

## STAMP-16 placement pipeline

Configure and build a Debug baseline from an MSVC developer prompt:

```powershell
cmake --preset windows-debug -DVF_BUILD_BENCHMARKS=ON
cmake --build --preset build-windows-debug --target VoxelForgeStampPlacementBenchmark
.\build\windows-debug\benchmarks\VoxelForgeStampPlacementBenchmark.exe `
  --csv .\build\windows-debug\stamp16-results.csv `
  --json .\build\windows-debug\stamp16-results.json
```

For comparable runs, keep the same machine, build preset, source revision,
graphics driver, power profile, and default dataset iteration counts. Close
other GPU-heavy applications.

The fixed datasets contain 64, 512, 4,096, 32,768, 131,072, and 262,144 solid
voxels. The executable measures:

- `PaletteMappingEngine`;
- `StampPlacementPlanner`;
- plan-to-preview adaptation;
- plan-to-operation adaptation;
- the composite document/history transaction excluding its nested mesh build;
- mesh rebuilds;
- the production renderer's GPU upload;
- end-to-end Undo and Redo.

`allocations`, `allocated_bytes`, `peak_live_bytes`, and `retained_bytes` cover
C++ `operator new` calls on the benchmark thread. SDL and graphics-driver
native allocations are outside that counter. `logical_copies` and
`logical_copy_bytes` are explicit payload-copy estimates derived from public
data contracts; they are not hardware performance-counter samples.

Renderer upload performs one untimed warm-up for each dataset size before its
timed calls. Mesh rebuild results are reported separately for Execute, Undo,
and Redo. Undo/Redo timings remain end-to-end and therefore include their
nested rebuild; composite-transaction timings explicitly subtract it.
