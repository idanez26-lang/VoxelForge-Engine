add_executable(
    VoxelForgeStampPlacementBenchmark
    StampPlacementBenchmarkMetrics.cpp
    StampPlacementPipelineBenchmark.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/EditorCamera.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/ViewportRenderer.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/VoxelViewportState.cpp
)

target_include_directories(
    VoxelForgeStampPlacementBenchmark
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/editor/src
)

target_link_libraries(
    VoxelForgeStampPlacementBenchmark
    PRIVATE
        VoxelForgeEditorCommands
        VoxelForgeEditorTransformGizmo
        VoxelForgeEditorVoxelSelection
        VoxelForgeEditorVoxelStamps
        VoxelForgeImGui
        VoxelForgeMesh
        VoxelForgeRenderer
)

target_compile_features(
    VoxelForgeStampPlacementBenchmark
    PRIVATE cxx_std_20
)

if(MSVC)
    target_compile_options(
        VoxelForgeStampPlacementBenchmark
        PRIVATE /W4 /permissive-
    )
else()
    target_compile_options(
        VoxelForgeStampPlacementBenchmark
        PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(WIN32)
    add_dependencies(
        VoxelForgeStampPlacementBenchmark
        VoxelForgeViewportShaders
    )
    target_compile_definitions(
        VoxelForgeStampPlacementBenchmark
        PRIVATE
            VOXELFORGE_VIEWPORT_VERTEX_SHADER="${PROJECT_BINARY_DIR}/editor/shaders/VoxelViewport.vert.dxil"
            VOXELFORGE_VIEWPORT_FRAGMENT_SHADER="${PROJECT_BINARY_DIR}/editor/shaders/VoxelViewport.frag.dxil"
    )
else()
    target_compile_definitions(
        VoxelForgeStampPlacementBenchmark
        PRIVATE
            VOXELFORGE_VIEWPORT_VERTEX_SHADER=""
            VOXELFORGE_VIEWPORT_FRAGMENT_SHADER=""
    )
endif()

# This target is deliberately absent from add_test(). Its workloads are
# profiling fixtures, not pass/fail regression tests.

# PERF-FOUNDATION lot 0: CPU-only baseline with percentiles, sparse scenes and
# the exact preview composer. No renderer, no ImGui: every measured entry point
# is a static function.
add_executable(
    VoxelForgePerformanceFoundationBaseline
    PerformanceFoundationBaseline.cpp
    StampPlacementBenchmarkMetrics.cpp
)
target_include_directories(
    VoxelForgePerformanceFoundationBaseline
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/editor/src
)
target_link_libraries(
    VoxelForgePerformanceFoundationBaseline
    PRIVATE
        VoxelForgeEditorCommands
        VoxelForgeEditorVoxelStamps
        VoxelForgeAsset
        VoxelForgeMesh
)
target_compile_features(
    VoxelForgePerformanceFoundationBaseline PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgePerformanceFoundationBaseline PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgePerformanceFoundationBaseline
        PRIVATE -Wall -Wextra -Wpedantic)
endif()

# VF-0262 lot 262-0: CPU-only baseline for the incremental mesh rebuild.
add_executable(
    VoxelForgeIncrementalEditBenchmark
    IncrementalEditBenchmark.cpp
)
target_link_libraries(
    VoxelForgeIncrementalEditBenchmark
    PRIVATE
        VoxelForgeAsset
        VoxelForgeMesh
)
target_compile_features(
    VoxelForgeIncrementalEditBenchmark
    PRIVATE cxx_std_20
)
if(MSVC)
    target_compile_options(
        VoxelForgeIncrementalEditBenchmark PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeIncrementalEditBenchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()
