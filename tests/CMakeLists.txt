function(vf_configure_test target)
    target_compile_features(${target} PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_executable(
    VoxelForgeEditorConsoleServiceTests
    Editor/EditorConsoleServiceTests.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/Console/EditorConsoleService.cpp
)
target_include_directories(
    VoxelForgeEditorConsoleServiceTests
    PRIVATE ${PROJECT_SOURCE_DIR}/editor/src
)
vf_configure_test(VoxelForgeEditorConsoleServiceTests)
add_test(
    NAME VoxelForge.Editor.ConsoleService
    COMMAND $<TARGET_FILE:VoxelForgeEditorConsoleServiceTests>
)

add_executable(VoxelForgeCoreEventTests Core/EventSystemTests.cpp)
target_link_libraries(VoxelForgeCoreEventTests PRIVATE VoxelForgeCore)
vf_configure_test(VoxelForgeCoreEventTests)
add_test(
    NAME VoxelForge.Core.Events
    COMMAND $<TARGET_FILE:VoxelForgeCoreEventTests>
)

add_executable(VoxelForgeCoreLayerTests Core/LayerSystemTests.cpp)
target_link_libraries(VoxelForgeCoreLayerTests PRIVATE VoxelForgeCore)
vf_configure_test(VoxelForgeCoreLayerTests)
add_test(
    NAME VoxelForge.Core.Layers
    COMMAND $<TARGET_FILE:VoxelForgeCoreLayerTests>
)

add_executable(VoxelForgeCoreUserDataPathsTests Core/UserDataPathsTests.cpp)
target_link_libraries(VoxelForgeCoreUserDataPathsTests PRIVATE VoxelForgeCore)
vf_configure_test(VoxelForgeCoreUserDataPathsTests)
add_test(
    NAME VoxelForge.Core.UserDataPaths
    COMMAND $<TARGET_FILE:VoxelForgeCoreUserDataPathsTests>
)

add_executable(
    VoxelForgeEditorExitRequestTests
    Editor/EditorExitRequestTests.cpp
)
target_include_directories(
    VoxelForgeEditorExitRequestTests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/editor/src
)
vf_configure_test(VoxelForgeEditorExitRequestTests)
add_test(
    NAME VoxelForge.Editor.ExitRequest
    COMMAND $<TARGET_FILE:VoxelForgeEditorExitRequestTests>
)

add_executable(
    VoxelForgeEditorSaveOnExitTests
    Editor/SaveOnExitTests.cpp
)
target_include_directories(
    VoxelForgeEditorSaveOnExitTests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/editor/src
)
vf_configure_test(VoxelForgeEditorSaveOnExitTests)
add_test(
    NAME VoxelForge.Editor.SaveOnExit
    COMMAND $<TARGET_FILE:VoxelForgeEditorSaveOnExitTests>
)

add_executable(
    VoxelForgeEditorWindowTitleTests
    Editor/EditorWindowTitleTests.cpp
)
target_include_directories(
    VoxelForgeEditorWindowTitleTests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/editor/src
)
vf_configure_test(VoxelForgeEditorWindowTitleTests)
add_test(
    NAME VoxelForge.Editor.WindowTitle
    COMMAND $<TARGET_FILE:VoxelForgeEditorWindowTitleTests>
)

add_executable(
    VoxelForgeEditorAssetBrowserTests
    Editor/AssetBrowserTests.cpp
)
target_link_libraries(
    VoxelForgeEditorAssetBrowserTests
    PRIVATE
        VoxelForgeEditorAssetBrowser
)
vf_configure_test(VoxelForgeEditorAssetBrowserTests)
add_test(
    NAME VoxelForge.Editor.AssetBrowser
    COMMAND $<TARGET_FILE:VoxelForgeEditorAssetBrowserTests>
)

add_executable(
    VoxelForgeEditorModelImportTests
    Editor/ModelImportTests.cpp
    Editor/DragDropImportTests.cpp
)
target_link_libraries(
    VoxelForgeEditorModelImportTests
    PRIVATE VoxelForgeEditorModelImport
)
vf_configure_test(VoxelForgeEditorModelImportTests)
add_test(
    NAME VoxelForge.Editor.ModelImport
    COMMAND $<TARGET_FILE:VoxelForgeEditorModelImportTests>
)
add_test(
    NAME VoxelForge.Editor.DragDropImport
    COMMAND $<TARGET_FILE:VoxelForgeEditorModelImportTests> --drag-drop-only
)
add_executable(
    VoxelForgeEditorFrameProbeTests
    Editor/EditorFrameProbeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorFrameProbeTests
    PRIVATE VoxelForgeEditorDiagnostics
)
vf_configure_test(VoxelForgeEditorFrameProbeTests)
add_test(
    NAME VoxelForge.Editor.FrameProbe
    COMMAND $<TARGET_FILE:VoxelForgeEditorFrameProbeTests>
)
add_executable(
    VoxelForgeEditorModelImportBatchTests
    Editor/ModelImportBatchTests.cpp
)
target_link_libraries(
    VoxelForgeEditorModelImportBatchTests
    PRIVATE VoxelForgeEditorModelImport
)
vf_configure_test(VoxelForgeEditorModelImportBatchTests)
add_test(
    NAME VoxelForge.Editor.ModelImportBatch
    COMMAND $<TARGET_FILE:VoxelForgeEditorModelImportBatchTests>
)

add_executable(
    VoxelForgeEditorAssetMetadataTests
    Editor/AssetMetadataTests.cpp
)
target_link_libraries(
    VoxelForgeEditorAssetMetadataTests
    PRIVATE
        VoxelForgeEditorAssetBrowser
        VoxelForgeEditorModelImport
)
vf_configure_test(VoxelForgeEditorAssetMetadataTests)
add_test(
    NAME VoxelForge.Editor.AssetMetadata
    COMMAND $<TARGET_FILE:VoxelForgeEditorAssetMetadataTests>
)

add_executable(
    VoxelForgeEditorAssetInspectorTests
    Editor/AssetInspectorTests.cpp
    Editor/VoxThumbnailTests.cpp
)
target_link_libraries(
    VoxelForgeEditorAssetInspectorTests
    PRIVATE
        VoxelForgeEditorAssetInspector
        VoxelForgeEditorModelImport
)
vf_configure_test(VoxelForgeEditorAssetInspectorTests)
add_test(
    NAME VoxelForge.Editor.AssetInspector
    COMMAND $<TARGET_FILE:VoxelForgeEditorAssetInspectorTests>
)
add_test(
    NAME VoxelForge.Editor.VoxThumbnail
    COMMAND $<TARGET_FILE:VoxelForgeEditorAssetInspectorTests> --thumbnail-only
)

add_executable(
    VoxelForgeProjectManagerTests
    Project/ProjectManagerTests.cpp
)

add_executable(
    VoxelForgeEditorVoxelViewportTests
    Editor/VoxelViewportTests.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/EditorCamera.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/VoxelViewportState.cpp
)
target_include_directories(
    VoxelForgeEditorVoxelViewportTests
    PRIVATE ${PROJECT_SOURCE_DIR}/editor/src
)
target_link_libraries(
    VoxelForgeEditorVoxelViewportTests
    PRIVATE VoxelForgeMesh VoxelForgeImGui
)
vf_configure_test(VoxelForgeEditorVoxelViewportTests)
add_test(
    NAME VoxelForge.Editor.VoxelViewport
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelViewportTests>
)

add_executable(
    VoxelForgeEditorUniversalCursor2DTests
    Editor/UniversalCursor2DTests.cpp
)
target_link_libraries(
    VoxelForgeEditorUniversalCursor2DTests
    PRIVATE VoxelForgeEditorVoxelPreview
)
vf_configure_test(VoxelForgeEditorUniversalCursor2DTests)
add_test(
    NAME VoxelForge.Editor.UniversalCursor2D
    COMMAND $<TARGET_FILE:VoxelForgeEditorUniversalCursor2DTests>
)

add_executable(
    VoxelForgeEditorDoubleClickCameraTests
    Editor/DoubleClickCameraTests.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/EditorCamera.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/ViewportInput/ViewportCameraInput.cpp
)
target_include_directories(
    VoxelForgeEditorDoubleClickCameraTests
    PRIVATE ${PROJECT_SOURCE_DIR}/editor/src
)
target_link_libraries(
    VoxelForgeEditorDoubleClickCameraTests
    PRIVATE VoxelForgeEditorVoxelSelection VoxelForgeImGui
)
vf_configure_test(VoxelForgeEditorDoubleClickCameraTests)
add_test(
    NAME VoxelForge.Editor.DoubleClickCamera
    COMMAND $<TARGET_FILE:VoxelForgeEditorDoubleClickCameraTests>
)

add_executable(
    VoxelForgeEditorViewportNavigationTests
    Editor/ViewportNavigationControllerTests.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/EditorCamera.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/ViewportNavigationController.cpp
)
target_include_directories(
    VoxelForgeEditorViewportNavigationTests
    PRIVATE ${PROJECT_SOURCE_DIR}/editor/src
)
target_link_libraries(
    VoxelForgeEditorViewportNavigationTests
    PRIVATE VoxelForgeEditorVoxelSelection VoxelForgeImGui
)
vf_configure_test(VoxelForgeEditorViewportNavigationTests)
add_test(
    NAME VoxelForge.Editor.ViewportNavigation
    COMMAND $<TARGET_FILE:VoxelForgeEditorViewportNavigationTests>
)

add_executable(
    VoxelForgeEditorVoxelSelectionTests
    Editor/VoxelSelectionTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelSelectionTests
    PRIVATE VoxelForgeEditorVoxelSelection
)
vf_configure_test(VoxelForgeEditorVoxelSelectionTests)
add_test(
    NAME VoxelForge.Editor.VoxelSelection
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelSelectionTests>
)

add_executable(
    VoxelForgeEditorVoxelRayPickingTests
    Editor/VoxelRayPickingTests.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/EditorCamera.cpp
)
target_include_directories(
    VoxelForgeEditorVoxelRayPickingTests
    PRIVATE ${PROJECT_SOURCE_DIR}/editor/src
)
target_link_libraries(
    VoxelForgeEditorVoxelRayPickingTests
    PRIVATE VoxelForgeEditorVoxelSelection VoxelForgeImGui
)
vf_configure_test(VoxelForgeEditorVoxelRayPickingTests)
add_test(
    NAME VoxelForge.Editor.VoxelRayPicking
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelRayPickingTests>
)

add_executable(
    VoxelForgeEditorCommandHistoryTests
    Editor/CommandHistoryTests.cpp
)
target_link_libraries(
    VoxelForgeEditorCommandHistoryTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorCommandHistoryTests)
add_test(
    NAME VoxelForge.Editor.CommandHistory
    COMMAND $<TARGET_FILE:VoxelForgeEditorCommandHistoryTests>
)
add_executable(
    VoxelForgeEditorEraseVoxelTests
    Editor/EraseVoxelTests.cpp
)
target_link_libraries(
    VoxelForgeEditorEraseVoxelTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorEraseVoxelTests)
add_test(
    NAME VoxelForge.Editor.EraseVoxel
    COMMAND $<TARGET_FILE:VoxelForgeEditorEraseVoxelTests>
)
add_executable(
    VoxelForgeEditorPaintVoxelTests
    Editor/PaintVoxelTests.cpp
)
target_link_libraries(
    VoxelForgeEditorPaintVoxelTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorPaintVoxelTests)
add_test(
    NAME VoxelForge.Editor.PaintVoxel
    COMMAND $<TARGET_FILE:VoxelForgeEditorPaintVoxelTests>
)
add_executable(
    VoxelForgeEditorAddVoxelTests
    Editor/AddVoxelTests.cpp
)
target_link_libraries(
    VoxelForgeEditorAddVoxelTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh VoxelForgeVoxel
)
vf_configure_test(VoxelForgeEditorAddVoxelTests)
add_test(
    NAME VoxelForge.Editor.AddVoxel
    COMMAND $<TARGET_FILE:VoxelForgeEditorAddVoxelTests>
)
add_executable(
    VoxelForgeEditorVoxelPencilTests
    Editor/VoxelPencilTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelPencilTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorVoxelPencilTests)
add_test(
    NAME VoxelForge.Editor.VoxelPencil
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelPencilTests>
)
add_executable(
    VoxelForgeEditorSmartBrushEngineTests
    Editor/SmartBrushEngineTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartBrushEngineTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorSmartBrushEngineTests)
add_test(
    NAME VoxelForge.Editor.SmartBrushEngine
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartBrushEngineTests>
)
add_executable(
    VoxelForgeEditorVoxelEraserTests
    Editor/VoxelEraserTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelEraserTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorVoxelEraserTests)
add_test(
    NAME VoxelForge.Editor.VoxelEraser
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelEraserTests>
)
add_executable(
    VoxelForgeEditorVoxelFillTests
    Editor/VoxelFillTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelFillTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeEditorPalette
)
vf_configure_test(VoxelForgeEditorVoxelFillTests)
add_test(
    NAME VoxelForge.Editor.VoxelFill
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelFillTests>
)
add_executable(
    VoxelForgeEditorVoxelPaintBrushTests
    Editor/VoxelPaintBrushTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelPaintBrushTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorVoxelPaintBrushTests)
add_test(
    NAME VoxelForge.Editor.VoxelPaintBrush
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelPaintBrushTests>
)
add_executable(
    VoxelForgeEditorVoxelBoxTests
    Editor/VoxelBoxTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelBoxTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeEditorPalette
)
vf_configure_test(VoxelForgeEditorVoxelBoxTests)
add_test(
    NAME VoxelForge.Editor.VoxelBox
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelBoxTests>
)
add_executable(
    VoxelForgeEditorVoxelLineTests
    Editor/VoxelLineTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelLineTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeEditorPalette
)
vf_configure_test(VoxelForgeEditorVoxelLineTests)
add_test(
    NAME VoxelForge.Editor.VoxelLine
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelLineTests>
)
add_executable(
    VoxelForgeEditorVoxelSphereTests
    Editor/VoxelSphereTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelSphereTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeEditorPalette
)
vf_configure_test(VoxelForgeEditorVoxelSphereTests)
add_test(
    NAME VoxelForge.Editor.VoxelSphere
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelSphereTests>
)
add_executable(
    VoxelForgeEditorToolbarTests
    Editor/EditorToolbarTests.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/Input/EditorInputService.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/Toolbar/EditorToolbarModel.cpp
)
target_include_directories(
    VoxelForgeEditorToolbarTests
    PRIVATE ${PROJECT_SOURCE_DIR}/editor/src
)
target_link_libraries(
    VoxelForgeEditorToolbarTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorToolbarTests)
add_test(
    NAME VoxelForge.Editor.ModernToolbar
    COMMAND $<TARGET_FILE:VoxelForgeEditorToolbarTests>
)
add_executable(
    VoxelForgeProfessionalToolFrameworkSmoke
    Editor/ToolFrameworkTests.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/Input/EditorInputService.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/Tools/ToolManager.cpp
)
add_executable(
    VoxelForgeEditorSmartToolTests
    Editor/SmartToolTests.cpp
)
target_link_libraries(VoxelForgeEditorSmartToolTests PRIVATE VoxelForgeEditorCommands)
vf_configure_test(VoxelForgeEditorSmartToolTests)
add_test(NAME VoxelForge.Editor.SmartTool COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolTests>)
add_executable(
    VoxelForgeEditorSmartToolFoundationTests
    Editor/SmartToolFoundationTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolFoundationTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolFoundationTests)
add_test(
    NAME VoxelForge.Editor.SmartToolFoundation
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolFoundationTests>
)
add_executable(
    VoxelForgeEditorSmartToolPlanStateTests
    Editor/SmartToolPlanStateTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolPlanStateTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolPlanStateTests)
add_test(
    NAME VoxelForge.Editor.SmartToolPlanState
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolPlanStateTests>
)
add_executable(
    VoxelForgeEditorSmartToolPlanStateSmokeTests
    Editor/SmartToolPlanStateSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolPlanStateSmokeTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorSmartToolPlanStateSmokeTests)
add_test(
    NAME VoxelForge.Editor.SmartToolPlanStateSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolPlanStateSmokeTests>
)

# SMART-03: preview is a cached presentation of the immutable plan only.
add_executable(
    VoxelForgeEditorSmartPreviewEngineTests
    Editor/SmartPreviewEngineTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartPreviewEngineTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartPreviewEngineTests)
add_test(
    NAME VoxelForge.Editor.SmartPreviewEngine
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartPreviewEngineTests>
)
add_executable(
    VoxelForgeEditorSmartToolExactPreviewComposerTests
    Editor/SmartToolExactPreviewComposerTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolExactPreviewComposerTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolExactPreviewComposerTests)
add_test(
    NAME VoxelForge.Editor.SmartToolExactPreviewComposer
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolExactPreviewComposerTests>
)

add_executable(
    VoxelForgeEditorSmartToolStrokeTests
    Editor/SmartToolStrokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolStrokeTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolStrokeTests)
add_test(
    NAME VoxelForge.Editor.SmartToolStroke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolStrokeTests>
)
add_executable(
    VoxelForgeEditorSmartToolStrokeSmokeTests
    Editor/SmartToolStrokeSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolStrokeSmokeTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolStrokeSmokeTests)
add_test(
    NAME VoxelForge.Editor.SmartToolStrokeSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolStrokeSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.SmartToolStrokeSmoke
    PROPERTIES LABELS "Smoke"
)

add_executable(
    VoxelForgeEditorSmartPreviewEngineSmokeTests
    Editor/SmartPreviewEngineSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartPreviewEngineSmokeTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartPreviewEngineSmokeTests)
add_test(
    NAME VoxelForge.Editor.SmartPreviewEngineSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartPreviewEngineSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.SmartPreviewEngineSmoke
    PROPERTIES LABELS "Smoke"
)

# SMART-04: canonical Single Voxel and Cube Brush modes share one planner.
add_executable(
    VoxelForgeEditorSmartToolModeTests
    Editor/SmartToolModeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolModeTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolModeTests)
add_test(
    NAME VoxelForge.Editor.SmartToolModes
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolModeTests>
)

# Viewport Interaction V2 Phase B: procedural compact Pencil planning is
# isolated from the legacy Smart Tool and deliberately has no CTest smoke/UI.
add_executable(
    VoxelForgeEditorPencilCompactPlanTests
    Editor/PencilCompactPlanTests.cpp
)
target_link_libraries(
    VoxelForgeEditorPencilCompactPlanTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorPencilCompactPlanTests)
add_test(
    NAME VoxelForge.Editor.PencilCompactPlan
    COMMAND $<TARGET_FILE:VoxelForgeEditorPencilCompactPlanTests>
)

add_executable(VoxelForgeEditorPencilInteractionV2Tests Editor/PencilInteractionV2Tests.cpp)
target_link_libraries(VoxelForgeEditorPencilInteractionV2Tests PRIVATE VoxelForgeEditorViewportInteractionV2)
vf_configure_test(VoxelForgeEditorPencilInteractionV2Tests)
add_test(NAME VoxelForge.Editor.PencilInteractionV2 COMMAND $<TARGET_FILE:VoxelForgeEditorPencilInteractionV2Tests>)

# Reproducible Pencil V2 scalability measurement.  It stays outside CTest:
# timing must not become a machine-dependent correctness gate.
add_executable(
    VoxelForgeEditorPencilInteractionV2Benchmark
    Editor/PencilInteractionV2Benchmark.cpp
)
target_link_libraries(
    VoxelForgeEditorPencilInteractionV2Benchmark
    PRIVATE VoxelForgeEditorViewportInteractionV2
)
vf_configure_test(VoxelForgeEditorPencilInteractionV2Benchmark)

add_executable(
    VoxelForgeEditorSmartToolFaceTests
    Editor/SmartToolFaceTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolFaceTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolFaceTests)
add_test(
    NAME VoxelForge.Editor.SmartToolFace
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolFaceTests>
)
add_executable(
    VoxelForgeEditorSmartToolLineTests
    Editor/SmartToolLineTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolLineTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolLineTests)
add_test(
    NAME VoxelForge.Editor.SmartToolLine
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolLineTests>
)
add_executable(
    VoxelForgeEditorSmartToolRectangleTests
    Editor/SmartToolRectangleTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolRectangleTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolRectangleTests)
add_test(
    NAME VoxelForge.Editor.SmartToolRectangle
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolRectangleTests>
)
add_executable(
    VoxelForgeEditorSmartToolSurfaceTests
    Editor/SmartToolSurfaceTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolSurfaceTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolSurfaceTests)
add_test(
    NAME VoxelForge.Editor.SmartToolSurface
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolSurfaceTests>
)
add_executable(
    VoxelForgeEditorSmartToolFillTests
    Editor/SmartToolFillTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolFillTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolFillTests)
add_test(
    NAME VoxelForge.Editor.SmartToolFill
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolFillTests>
)
add_executable(
    VoxelForgeEditorSmartToolFillSmokeTests
    Editor/SmartToolFillSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolFillSmokeTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorSmartToolFillSmokeTests)
add_test(
    NAME VoxelForge.Editor.SmartToolFillSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolFillSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.SmartToolFillSmoke
    PROPERTIES LABELS "Smoke"
)
add_executable(
    VoxelForgeEditorSmartToolSurfaceSmokeTests
    Editor/SmartToolSurfaceSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolSurfaceSmokeTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorSmartToolSurfaceSmokeTests)
add_test(
    NAME VoxelForge.Editor.SmartToolSurfaceSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolSurfaceSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.SmartToolSurfaceSmoke
    PROPERTIES LABELS "Smoke"
)
add_executable(
    VoxelForgeEditorSmartToolRectangleSmokeTests
    Editor/SmartToolRectangleSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolRectangleSmokeTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorSmartToolRectangleSmokeTests)
add_test(
    NAME VoxelForge.Editor.SmartToolRectangleSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolRectangleSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.SmartToolRectangleSmoke
    PROPERTIES LABELS "Smoke"
)
add_executable(
    VoxelForgeEditorSmartToolLineConstraintResolverTests
    Editor/SmartToolLineConstraintResolverTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolLineConstraintResolverTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolLineConstraintResolverTests)
add_test(
    NAME VoxelForge.Editor.SmartToolLineConstraints
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolLineConstraintResolverTests>
)
add_executable(
    VoxelForgeEditorSmartToolLineSmokeTests
    Editor/SmartToolLineSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolLineSmokeTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorSmartToolLineSmokeTests)
add_test(
    NAME VoxelForge.Editor.SmartToolLineSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolLineSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.SmartToolLineSmoke
    PROPERTIES LABELS "Smoke"
)
add_executable(
    VoxelForgeEditorSmartToolLineConstraintSmokeTests
    Editor/SmartToolLineConstraintSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolLineConstraintSmokeTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorSmartToolLineConstraintSmokeTests)
add_test(
    NAME VoxelForge.Editor.SmartToolLineConstraintSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolLineConstraintSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.SmartToolLineConstraintSmoke
    PROPERTIES LABELS "Smoke"
)
add_executable(
    VoxelForgeEditorSmartToolFaceSmokeTests
    Editor/SmartToolFaceSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolFaceSmokeTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorSmartToolFaceSmokeTests)
add_test(
    NAME VoxelForge.Editor.SmartToolFaceSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolFaceSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.SmartToolFaceSmoke
    PROPERTIES LABELS "Smoke"
)

add_executable(
    VoxelForgeEditorSmartToolModeSmokeTests
    Editor/SmartToolModeSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolModeSmokeTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolModeSmokeTests)
add_test(
    NAME VoxelForge.Editor.SmartToolModesSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolModeSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.SmartToolModesSmoke
    PROPERTIES LABELS "Smoke"
)
add_executable(
    VoxelForgeEditorSmartToolBrushMode2DSmokeTests
    Editor/SmartToolBrushMode2DSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSmartToolBrushMode2DSmokeTests
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorSmartToolBrushMode2DSmokeTests)
add_test(
    NAME VoxelForge.Editor.SmartToolBrushMode2DSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSmartToolBrushMode2DSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.SmartToolBrushMode2DSmoke
    PROPERTIES LABELS "Smoke"
)
set_tests_properties(
    VoxelForge.Editor.SmartToolPlanStateSmoke
    PROPERTIES LABELS "Smoke"
)
add_executable(VoxelForgeEditorBrushProfileTests Editor/BrushProfileServiceTests.cpp)
target_link_libraries(VoxelForgeEditorBrushProfileTests PRIVATE VoxelForgeEditorCommands)
vf_configure_test(VoxelForgeEditorBrushProfileTests)
add_test(NAME VoxelForge.Editor.BrushProfiles COMMAND $<TARGET_FILE:VoxelForgeEditorBrushProfileTests>)
add_executable(
    VoxelForgeEditorProfessionalGhostPreviewSmoke
    Editor/ProfessionalGhostPreviewSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorProfessionalGhostPreviewSmoke
    PRIVATE VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorProfessionalGhostPreviewSmoke)
add_test(
    NAME VoxelForge.Editor.ProfessionalGhostPreviewSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorProfessionalGhostPreviewSmoke>
)
target_include_directories(
    VoxelForgeProfessionalToolFrameworkSmoke
    PRIVATE ${PROJECT_SOURCE_DIR}/editor/src
)
target_link_libraries(
    VoxelForgeProfessionalToolFrameworkSmoke
    PRIVATE VoxelForgeEditorCommands VoxelForgeEditorTransformPivot
)
vf_configure_test(VoxelForgeProfessionalToolFrameworkSmoke)
add_test(
    NAME VoxelForge.Editor.ProfessionalToolFrameworkSmoke
    COMMAND $<TARGET_FILE:VoxelForgeProfessionalToolFrameworkSmoke>
)
add_executable(
    VoxelForgeEditorInputServiceTests
    Editor/EditorInputServiceTests.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/Input/EditorInputService.cpp
)
target_include_directories(
    VoxelForgeEditorInputServiceTests
    PRIVATE ${PROJECT_SOURCE_DIR}/editor/src
)
vf_configure_test(VoxelForgeEditorInputServiceTests)
add_test(
    NAME VoxelForge.Editor.KeyboardShortcuts
    COMMAND $<TARGET_FILE:VoxelForgeEditorInputServiceTests>
)
add_executable(
    VoxelForgeEditorSelectionServiceTests
    Editor/SelectionServiceTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSelectionServiceTests
    PRIVATE VoxelForgeEditorVoxelSelection
)
vf_configure_test(VoxelForgeEditorSelectionServiceTests)
add_test(
    NAME VoxelForge.Editor.SelectionService
    COMMAND $<TARGET_FILE:VoxelForgeEditorSelectionServiceTests>
)
add_executable(
    VoxelForgeEditorTransformPreviewTests
    Editor/TransformPreviewTests.cpp
)
target_link_libraries(
    VoxelForgeEditorTransformPreviewTests
    PRIVATE VoxelForgeEditorTransformPreview
)
vf_configure_test(VoxelForgeEditorTransformPreviewTests)
add_test(
    NAME VoxelForge.Editor.TransformPreview
    COMMAND $<TARGET_FILE:VoxelForgeEditorTransformPreviewTests>
)
add_executable(
    VoxelForgeEditorVoxelPlacementPreviewTests
    Editor/VoxelPlacementPreviewTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelPlacementPreviewTests
    PRIVATE VoxelForgeEditorTransformPreview
)
vf_configure_test(VoxelForgeEditorVoxelPlacementPreviewTests)
add_test(
    NAME VoxelForge.Editor.VoxelPlacementPreview
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelPlacementPreviewTests>
)
add_executable(
    VoxelForgeEditorViewportInteractionV2Tests
    Editor/ViewportInteractionV2Tests.cpp
)
target_link_libraries(
    VoxelForgeEditorViewportInteractionV2Tests
    PRIVATE VoxelForgeEditorViewportInteractionV2
)
vf_configure_test(VoxelForgeEditorViewportInteractionV2Tests)
add_test(
    NAME VoxelForge.Editor.ViewportInteractionV2
    COMMAND $<TARGET_FILE:VoxelForgeEditorViewportInteractionV2Tests>
)
set_tests_properties(
    VoxelForge.Editor.ViewportInteractionV2 PROPERTIES TIMEOUT 120)

# Reproducible scalability benchmark. It is intentionally not registered with
# CTest because timing data must remain separate from correctness gates.
add_executable(
    VoxelForgeEditorViewportInteractionV2MoveBenchmark
    Editor/ViewportInteractionV2MoveBenchmark.cpp
)
target_link_libraries(
    VoxelForgeEditorViewportInteractionV2MoveBenchmark
    PRIVATE VoxelForgeEditorViewportInteractionV2
)
vf_configure_test(VoxelForgeEditorViewportInteractionV2MoveBenchmark)

# Deterministic 36,000-frame (10-minute equivalent at 60 FPS) soak
# instrumentation. It remains outside CTest because timing is a diagnostic,
# not a machine-dependent correctness gate.
add_executable(
    VoxelForgeEditorViewportInteractionV2SoakBenchmark
    Editor/ViewportInteractionV2SoakBenchmark.cpp
)
target_link_libraries(
    VoxelForgeEditorViewportInteractionV2SoakBenchmark
    PRIVATE VoxelForgeEditorViewportInteractionV2
)
vf_configure_test(VoxelForgeEditorViewportInteractionV2SoakBenchmark)

add_executable(
    VoxelForgeEditorViewportInteractionV2SmokeTests
    Editor/ViewportInteractionV2SmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorViewportInteractionV2SmokeTests
    PRIVATE VoxelForgeEditorViewportInteractionV2
)
vf_configure_test(VoxelForgeEditorViewportInteractionV2SmokeTests)
add_test(
    NAME VoxelForge.Editor.ViewportInteractionV2Smoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorViewportInteractionV2SmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.ViewportInteractionV2Smoke
    PROPERTIES LABELS "Smoke" TIMEOUT 30)
add_executable(
    VoxelForgeEditorTransformGizmoTests
    Editor/TransformGizmoTests.cpp
)
target_link_libraries(
    VoxelForgeEditorTransformGizmoTests
    PRIVATE
        VoxelForgeEditorTransformGizmo
        VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorTransformGizmoTests)
add_test(
    NAME VoxelForge.Editor.TransformGizmo
    COMMAND $<TARGET_FILE:VoxelForgeEditorTransformGizmoTests>
)
add_executable(
    VoxelForgeEditorTransformGizmoManagerTests
    Editor/TransformGizmoManagerTests.cpp
)
target_link_libraries(
    VoxelForgeEditorTransformGizmoManagerTests
    PRIVATE VoxelForgeEditorTransformGizmo
)
vf_configure_test(VoxelForgeEditorTransformGizmoManagerTests)
add_test(
    NAME VoxelForge.Editor.TransformGizmoManager
    COMMAND $<TARGET_FILE:VoxelForgeEditorTransformGizmoManagerTests>
)
add_executable(
    VoxelForgeEditorTransformPivotTests
    Editor/TransformPivotTests.cpp
)
target_link_libraries(
    VoxelForgeEditorTransformPivotTests
    PRIVATE VoxelForgeEditorTransformPivot
)
vf_configure_test(VoxelForgeEditorTransformPivotTests)
add_test(
    NAME VoxelForge.Editor.TransformPivot
    COMMAND $<TARGET_FILE:VoxelForgeEditorTransformPivotTests>
)
add_executable(
    VoxelForgeEditorTransformPanelTests
    Editor/TransformPanelViewModelTests.cpp
)
target_link_libraries(
    VoxelForgeEditorTransformPanelTests
    PRIVATE VoxelForgeEditorTransformPanel
)
vf_configure_test(VoxelForgeEditorTransformPanelTests)
add_test(
    NAME VoxelForge.Editor.TransformPanel
    COMMAND $<TARGET_FILE:VoxelForgeEditorTransformPanelTests>
)
add_executable(
    VoxelForgeEditorConstraintEngineTests
    Editor/ConstraintEngineTests.cpp
)
target_link_libraries(
    VoxelForgeEditorConstraintEngineTests
    PRIVATE VoxelForgeEditorConstraints
)
vf_configure_test(VoxelForgeEditorConstraintEngineTests)
add_test(
    NAME VoxelForge.Editor.ConstraintEngine
    COMMAND $<TARGET_FILE:VoxelForgeEditorConstraintEngineTests>
)
add_executable(
    VoxelForgeEditorMoveGizmoTests
    Editor/MoveGizmoTests.cpp
)
target_link_libraries(
    VoxelForgeEditorMoveGizmoTests
    PRIVATE VoxelForgeEditorTransformGizmo
)
vf_configure_test(VoxelForgeEditorMoveGizmoTests)
add_test(
    NAME VoxelForge.Editor.MoveGizmo
    COMMAND $<TARGET_FILE:VoxelForgeEditorMoveGizmoTests>
)

add_executable(
    VoxelForgeEditorRotateGizmoTests
    Editor/RotateGizmoTests.cpp
)
target_link_libraries(
    VoxelForgeEditorRotateGizmoTests
    PRIVATE VoxelForgeEditorTransformGizmo VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorRotateGizmoTests)
add_test(
    NAME VoxelForge.Editor.RotateGizmo
    COMMAND $<TARGET_FILE:VoxelForgeEditorRotateGizmoTests>
)
add_executable(
    VoxelForgeEditorScaleGizmoTests
    Editor/ScaleGizmoTests.cpp
)
target_link_libraries(
    VoxelForgeEditorScaleGizmoTests
    PRIVATE
        VoxelForgeEditorTransformGizmo
        VoxelForgeEditorCommands
        VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorScaleGizmoTests)
add_test(
    NAME VoxelForge.Editor.ScaleGizmo
    COMMAND $<TARGET_FILE:VoxelForgeEditorScaleGizmoTests>
)
add_executable(
    VoxelForgeEditorTransformOperationFrameworkTests
    Editor/TransformOperationFrameworkTests.cpp
)
target_link_libraries(
    VoxelForgeEditorTransformOperationFrameworkTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorTransformOperationFrameworkTests)
add_test(
    NAME VoxelForge.Editor.TransformOperationFramework
    COMMAND $<TARGET_FILE:VoxelForgeEditorTransformOperationFrameworkTests>
)
add_executable(
    VoxelForgeEditorDialogTests
    Editor/EditorDialogTests.cpp
)
target_link_libraries(
    VoxelForgeEditorDialogTests
    PRIVATE VoxelForgeEditorDialogs
)
vf_configure_test(VoxelForgeEditorDialogTests)
add_test(
    NAME VoxelForge.Editor.ModernDialogs
    COMMAND $<TARGET_FILE:VoxelForgeEditorDialogTests>
)
add_executable(
    VoxelForgeEditorVoxelEditHistoryTests
    Editor/VoxelEditHistoryTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelEditHistoryTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorVoxelEditHistoryTests)
add_test(
    NAME VoxelForge.Editor.VoxelEditHistory
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelEditHistoryTests>
)
add_executable(
    VoxelForgeEditorCompositeVoxelEditSmokeTests
    Editor/CompositeVoxelEditSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorCompositeVoxelEditSmokeTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorCompositeVoxelEditSmokeTests)
add_test(
    NAME VoxelForge.Editor.CompositeVoxelEditSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorCompositeVoxelEditSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.CompositeVoxelEditSmoke
    PROPERTIES LABELS "Smoke"
)
add_executable(
    VoxelForgeEditorVoxelMoveTests
    Editor/VoxelMoveTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelMoveTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorVoxelMoveTests)
add_test(
    NAME VoxelForge.Editor.VoxelMove
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelMoveTests>
)
add_executable(
    VoxelForgeEditorVoxelDuplicateTests
    Editor/VoxelDuplicateTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelDuplicateTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorVoxelDuplicateTests)
add_test(
    NAME VoxelForge.Editor.VoxelDuplicate
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelDuplicateTests>
)
add_executable(
    VoxelForgeEditorVoxelRotateTests
    Editor/VoxelRotateTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelRotateTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorVoxelRotateTests)
add_test(
    NAME VoxelForge.Editor.VoxelRotate
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelRotateTests>
)
add_executable(
    VoxelForgeEditorVoxelMirrorTests
    Editor/VoxelMirrorTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelMirrorTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorVoxelMirrorTests)
add_test(
    NAME VoxelForge.Editor.VoxelMirror
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelMirrorTests>
)
add_executable(
    VoxelForgeEditorVoxelScaleTests
    Editor/VoxelScaleTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelScaleTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorVoxelScaleTests)
add_test(
    NAME VoxelForge.Editor.VoxelScale
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelScaleTests>
)
add_executable(
    VoxelForgeEditorVoxelAlignTests
    Editor/VoxelAlignTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelAlignTests
    PRIVATE VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorVoxelAlignTests)
add_test(
    NAME VoxelForge.Editor.VoxelAlign
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelAlignTests>
)
add_executable(
    VoxelForgeEditorQualityOfLifeTests
    Editor/QualityOfLifeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorQualityOfLifeTests
    PRIVATE VoxelForgeEditorQualityOfLife
)
vf_configure_test(VoxelForgeEditorQualityOfLifeTests)
add_test(
    NAME VoxelForge.Editor.QualityOfLife
    COMMAND $<TARGET_FILE:VoxelForgeEditorQualityOfLifeTests>
)
add_executable(
    VoxelForgeEditorVoxelSaveTests
    Editor/VoxelSaveTests.cpp
)
target_include_directories(
    VoxelForgeEditorVoxelSaveTests
    PRIVATE ${PROJECT_SOURCE_DIR}/editor/src
)
target_link_libraries(
    VoxelForgeEditorVoxelSaveTests
    PRIVATE VoxelForgeVoxel
)
vf_configure_test(VoxelForgeEditorVoxelSaveTests)
add_test(
    NAME VoxelForge.Editor.VoxelSave
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelSaveTests>
)
add_executable(
    VoxelForgeEditorVoxelDocumentSaveServiceTests
    Editor/VoxelDocumentSaveServiceTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelDocumentSaveServiceTests
    PRIVATE VoxelForgeEditorVoxelSave
)
vf_configure_test(VoxelForgeEditorVoxelDocumentSaveServiceTests)
add_test(
    NAME VoxelForge.Editor.VoxelDocumentSaveService
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelDocumentSaveServiceTests>
)
add_executable(
    VoxelForgeEditorFirstCreationExperienceTests
    Editor/FirstCreationExperienceTests.cpp
)
target_link_libraries(
    VoxelForgeEditorFirstCreationExperienceTests
    PRIVATE
        VoxelForgeEditorVoxelCreation
        VoxelForgeEditorVoxelSave
        VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorFirstCreationExperienceTests)
add_test(
    NAME VoxelForge.Editor.FirstCreationExperience
    COMMAND $<TARGET_FILE:VoxelForgeEditorFirstCreationExperienceTests>
)
add_executable(
    VoxelForgeEditorDirectCreationFlowTests
    Editor/DirectCreationFlowTests.cpp
)
target_link_libraries(
    VoxelForgeEditorDirectCreationFlowTests
    PRIVATE VoxelForgeEditorVoxelCreation
)
vf_configure_test(VoxelForgeEditorDirectCreationFlowTests)
add_test(
    NAME VoxelForge.Editor.DirectCreationFlow
    COMMAND $<TARGET_FILE:VoxelForgeEditorDirectCreationFlowTests>
)

add_executable(
    VoxelForgeEditorNewVoxelModelWorkflowTests
    Editor/NewVoxelModelWorkflowTests.cpp
)
target_link_libraries(
    VoxelForgeEditorNewVoxelModelWorkflowTests
    PRIVATE
        VoxelForgeEditorVoxelCreation
        VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorNewVoxelModelWorkflowTests)
add_test(
    NAME VoxelForge.Editor.NewVoxelModelWorkflow
    COMMAND $<TARGET_FILE:VoxelForgeEditorNewVoxelModelWorkflowTests>
)

add_executable(
    VoxelForgeEditorWelcomeExperienceTests
    Editor/WelcomeExperienceTests.cpp
)
target_link_libraries(
    VoxelForgeEditorWelcomeExperienceTests
    PRIVATE
        VoxelForgeEditorWelcome
        VoxelForgeProject
)
vf_configure_test(VoxelForgeEditorWelcomeExperienceTests)
add_test(
    NAME VoxelForge.Editor.WelcomeExperience
    COMMAND $<TARGET_FILE:VoxelForgeEditorWelcomeExperienceTests>
)
add_executable(
    VoxelForgeEditorPaletteServiceTests
    Editor/PaletteServiceTests.cpp
)
target_link_libraries(
    VoxelForgeEditorPaletteServiceTests
    PRIVATE VoxelForgeEditorPalette
)
vf_configure_test(VoxelForgeEditorPaletteServiceTests)
add_test(
    NAME VoxelForge.Editor.PaletteService
    COMMAND $<TARGET_FILE:VoxelForgeEditorPaletteServiceTests>
)
add_executable(
    VoxelForgeEditorPersistentWorkplaneTests
    Editor/PersistentWorkplaneTests.cpp
)
target_link_libraries(
    VoxelForgeEditorPersistentWorkplaneTests
    PRIVATE
        VoxelForgeEditorVoxelCreation
        VoxelForgeEditorVoxelSave
        VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorPersistentWorkplaneTests)
add_test(
    NAME VoxelForge.Editor.PersistentWorkplane
    COMMAND $<TARGET_FILE:VoxelForgeEditorPersistentWorkplaneTests>
)
add_executable(
    VoxelForgeEditorProjectSessionTests
    Editor/ProjectSessionTests.cpp
    ${PROJECT_SOURCE_DIR}/editor/src/EditorCamera.cpp
)
target_include_directories(
    VoxelForgeEditorProjectSessionTests
    PRIVATE ${PROJECT_SOURCE_DIR}/editor/src
)
target_link_libraries(
    VoxelForgeEditorProjectSessionTests
    PRIVATE VoxelForgeEditorProjectSession VoxelForgeImGui
)
vf_configure_test(VoxelForgeEditorProjectSessionTests)
add_test(
    NAME VoxelForge.Editor.ProjectSession
    COMMAND $<TARGET_FILE:VoxelForgeEditorProjectSessionTests>
)
add_executable(
    VoxelForgeEditorProjectSessionMappingTests
    Editor/ProjectSessionMappingTests.cpp
)
target_include_directories(
    VoxelForgeEditorProjectSessionMappingTests
    PRIVATE ${PROJECT_SOURCE_DIR}/editor/src
)
target_link_libraries(
    VoxelForgeEditorProjectSessionMappingTests
    PRIVATE VoxelForgeEditorProjectSession
)
vf_configure_test(VoxelForgeEditorProjectSessionMappingTests)
add_test(
    NAME VoxelForge.Editor.ProjectSessionMapping
    COMMAND $<TARGET_FILE:VoxelForgeEditorProjectSessionMappingTests>
)
add_executable(
    VoxelForgeEditorLayoutStabilityTests
    Editor/LayoutStabilityTests.cpp
)
target_link_libraries(
    VoxelForgeEditorLayoutStabilityTests
    PRIVATE VoxelForgeEditorLayout
)
vf_configure_test(VoxelForgeEditorLayoutStabilityTests)
add_test(
    NAME VoxelForge.Editor.LayoutStability
    COMMAND $<TARGET_FILE:VoxelForgeEditorLayoutStabilityTests>
)
target_link_libraries(
    VoxelForgeProjectManagerTests
    PRIVATE
        VoxelForgeProject
)
vf_configure_test(VoxelForgeProjectManagerTests)
add_test(
    NAME VoxelForge.Project.Manager
    COMMAND $<TARGET_FILE:VoxelForgeProjectManagerTests>
)

add_executable(
    VoxelForgeAssetVoxImporterTests
    Asset/VoxImporterTests.cpp
)
target_link_libraries(
    VoxelForgeAssetVoxImporterTests
    PRIVATE
        VoxelForgeAsset
)
vf_configure_test(VoxelForgeAssetVoxImporterTests)
add_test(
    NAME VoxelForge.Asset.VoxImporter
    COMMAND $<TARGET_FILE:VoxelForgeAssetVoxImporterTests>
)

add_executable(
    VoxelForgeAssetVoxModelAnalyzerTests
    Asset/VoxModelAnalyzerTests.cpp
)
target_link_libraries(
    VoxelForgeAssetVoxModelAnalyzerTests
    PRIVATE VoxelForgeAsset
)
vf_configure_test(VoxelForgeAssetVoxModelAnalyzerTests)
add_test(
    NAME VoxelForge.Asset.VoxModelAnalyzer
    COMMAND $<TARGET_FILE:VoxelForgeAssetVoxModelAnalyzerTests>
)

add_executable(
    VoxelForgeAssetVoxelDocumentTests
    Asset/VoxelDocumentTests.cpp
)
target_link_libraries(
    VoxelForgeAssetVoxelDocumentTests
    PRIVATE VoxelForgeEditorVoxelDocument
)
vf_configure_test(VoxelForgeAssetVoxelDocumentTests)
add_test(
    NAME VoxelForge.Asset.VoxelDocument
    COMMAND $<TARGET_FILE:VoxelForgeAssetVoxelDocumentTests>
)

# VF-0265 lot 1: ValidateVoxelChanges is the single implementation of the voxel
# change rejection rules, and it must mutate nothing.
add_executable(
    VoxelForgeAssetVoxelDocumentValidationTests
    Asset/VoxelDocumentValidationTests.cpp
)
target_link_libraries(
    VoxelForgeAssetVoxelDocumentValidationTests
    PRIVATE VoxelForgeEditorVoxelDocument
)
vf_configure_test(VoxelForgeAssetVoxelDocumentValidationTests)
add_test(
    NAME VoxelForge.Asset.VoxelDocumentValidation
    COMMAND $<TARGET_FILE:VoxelForgeAssetVoxelDocumentValidationTests>
)

add_executable(
    VoxelForgeAssetVoxDocumentWriterTests
    Asset/VoxDocumentWriterTests.cpp
)
target_link_libraries(
    VoxelForgeAssetVoxDocumentWriterTests
    PRIVATE VoxelForgeAsset
)
vf_configure_test(VoxelForgeAssetVoxDocumentWriterTests)
add_test(
    NAME VoxelForge.Asset.VoxDocumentWriter
    COMMAND $<TARGET_FILE:VoxelForgeAssetVoxDocumentWriterTests>
)

add_executable(
    VoxelForgeVoxelCoreTests
    Voxel/VoxelCoreTests.cpp
)
target_link_libraries(
    VoxelForgeVoxelCoreTests
    PRIVATE
        VoxelForgeVoxConversion
)
vf_configure_test(VoxelForgeVoxelCoreTests)
add_test(
    NAME VoxelForge.Voxel.Core
    COMMAND $<TARGET_FILE:VoxelForgeVoxelCoreTests>
)

add_executable(
    VoxelForgeVoxelSerializerTests
    Voxel/VoxelModelSerializerTests.cpp
)
target_link_libraries(
    VoxelForgeVoxelSerializerTests
    PRIVATE
        VoxelForgeVoxel
)
vf_configure_test(VoxelForgeVoxelSerializerTests)
add_test(
    NAME VoxelForge.Voxel.Serializer
    COMMAND $<TARGET_FILE:VoxelForgeVoxelSerializerTests>
)

add_executable(
    VoxelForgeMeshBuilderTests
    Mesh/VoxelMeshBuilderTests.cpp
)
target_link_libraries(
    VoxelForgeMeshBuilderTests
    PRIVATE
        VoxelForgeMesh
)
vf_configure_test(VoxelForgeMeshBuilderTests)
add_test(
    NAME VoxelForge.Mesh.VoxelMeshBuilder
    COMMAND $<TARGET_FILE:VoxelForgeMeshBuilderTests>
)

add_executable(
    VoxelForgeMeshDocumentSyncTests
    Mesh/VoxelDocumentMeshSyncTests.cpp
)
target_link_libraries(
    VoxelForgeMeshDocumentSyncTests
    PRIVATE VoxelForgeMesh
)
vf_configure_test(VoxelForgeMeshDocumentSyncTests)
add_test(
    NAME VoxelForge.Mesh.VoxelDocumentSync
    COMMAND $<TARGET_FILE:VoxelForgeMeshDocumentSyncTests>
)

# VF-0265 lot 2: the golden test of the "document + pending changes" view.
# Meshing the final state region by region must yield exactly the faces of a
# full build of a really mutated document — Preview == Commit, proven.
add_executable(
    VoxelForgeMeshVoxelChangeOverlayTests
    Mesh/VoxelChangeOverlayTests.cpp
)
target_link_libraries(
    VoxelForgeMeshVoxelChangeOverlayTests
    PRIVATE VoxelForgeMesh
)
vf_configure_test(VoxelForgeMeshVoxelChangeOverlayTests)
add_test(
    NAME VoxelForge.Mesh.VoxelChangeOverlay
    COMMAND $<TARGET_FILE:VoxelForgeMeshVoxelChangeOverlayTests>
)

# VF-0265 lot 3: the face filter lets a preview reuse a chunk that merely
# intersects the dirty region instead of rebuilding it (28 ms on a dense
# document). Filter + regional build must recompose the chunk exactly.
add_executable(
    VoxelForgeMeshDataFaceFilterTests
    Mesh/MeshDataFaceFilterTests.cpp
)
target_link_libraries(
    VoxelForgeMeshDataFaceFilterTests
    PRIVATE VoxelForgeMesh
)
vf_configure_test(VoxelForgeMeshDataFaceFilterTests)
add_test(
    NAME VoxelForge.Mesh.MeshDataFaceFilter
    COMMAND $<TARGET_FILE:VoxelForgeMeshDataFaceFilterTests>
)

set_tests_properties(
    VoxelForge.Core.Events
    VoxelForge.Core.Layers
    VoxelForge.Editor.ExitRequest
    VoxelForge.Editor.SaveOnExit
    VoxelForge.Editor.WindowTitle
    VoxelForge.Editor.AssetBrowser
    VoxelForge.Editor.ModelImport
    VoxelForge.Editor.DragDropImport
    VoxelForge.Editor.AssetMetadata
    VoxelForge.Editor.AssetInspector
    VoxelForge.Editor.VoxThumbnail
    VoxelForge.Editor.VoxelViewport
    VoxelForge.Editor.DoubleClickCamera
    VoxelForge.Editor.ViewportNavigation
    VoxelForge.Editor.VoxelSelection
    VoxelForge.Editor.VoxelRayPicking
    VoxelForge.Editor.CommandHistory
    VoxelForge.Editor.EraseVoxel
    VoxelForge.Editor.PaintVoxel
    VoxelForge.Editor.AddVoxel
    VoxelForge.Editor.VoxelPencil
    VoxelForge.Editor.VoxelEraser
    VoxelForge.Editor.VoxelFill
    VoxelForge.Editor.VoxelBox
    VoxelForge.Editor.VoxelLine
    VoxelForge.Editor.VoxelSphere
    VoxelForge.Editor.ModernToolbar
    VoxelForge.Editor.KeyboardShortcuts
    VoxelForge.Editor.SelectionService
    VoxelForge.Editor.TransformPreview
    VoxelForge.Editor.TransformGizmo
    VoxelForge.Editor.TransformGizmoManager
    VoxelForge.Editor.TransformPivot
    VoxelForge.Editor.TransformPanel
    VoxelForge.Editor.MoveGizmo
    VoxelForge.Editor.ScaleGizmo
    VoxelForge.Editor.VoxelRotate
    VoxelForge.Editor.VoxelMirror
    VoxelForge.Editor.VoxelScale
    VoxelForge.Editor.VoxelAlign
    VoxelForge.Editor.VoxelEditHistory
    VoxelForge.Editor.QualityOfLife
    VoxelForge.Editor.VoxelSave
    VoxelForge.Editor.VoxelDocumentSaveService
    VoxelForge.Editor.FirstCreationExperience
    VoxelForge.Editor.DirectCreationFlow
    VoxelForge.Editor.PaletteService
    VoxelForge.Editor.PersistentWorkplane
    VoxelForge.Editor.ProjectSession
    VoxelForge.Editor.LayoutStability
    VoxelForge.Project.Manager
    VoxelForge.Asset.VoxImporter
    VoxelForge.Asset.VoxModelAnalyzer
    VoxelForge.Asset.VoxelDocument
    VoxelForge.Asset.VoxDocumentWriter
    VoxelForge.Voxel.Core
    VoxelForge.Voxel.Serializer
    VoxelForge.Mesh.VoxelMeshBuilder
    VoxelForge.Mesh.VoxelDocumentSync
    PROPERTIES TIMEOUT 10
)

if(TARGET VoxelForgeEditor)
    add_test(
        NAME VoxelForge.Editor.Smoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelViewportSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --viewport-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelSelectionSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-selection-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.SelectionSystemSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --selection-system-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.TransformPreviewSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --transform-preview-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.EraseVoxelSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --erase-voxel-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.PaintVoxelSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --paint-voxel-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.QualityOfLifeSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --quality-of-life-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelSaveSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-save-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.AddVoxelSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --add-voxel-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.ModelImportSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --model-import-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.DragDropImportSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --drag-drop-import-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelDocumentSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-document-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelRenderSyncSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-render-sync-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelRayPickingSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-ray-picking-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelPencilSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-pencil-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelEraserSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-eraser-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelUndoRedoSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-undo-redo-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.FirstCreationExperienceSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --first-creation-experience-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.LayoutStabilitySmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --layout-stability-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.CreateWorkspaceSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --create-workspace-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.DoubleClickCameraSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --double-click-camera-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.ViewportNavigationSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --viewport-navigation-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.PersistentWorkplaneSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --persistent-construction-plane-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.ProjectSessionRestoreSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --project-session-restore-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.DirectCreationFlowSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --direct-creation-flow-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.InstantNewModelSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --instant-new-model-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.PaletteUiSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --palette-ui-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelFillSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-fill-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelBoxSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-box-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelLineSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-line-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelSphereSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-sphere-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.ModernToolbarSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --modern-toolbar-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.KeyboardShortcutsSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --keyboard-shortcuts-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.ModernDialogsSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --modern-dialogs-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.WelcomeScreenSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --welcome-screen-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.RemoveRecentProjectSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --remove-recent-project-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.SafeProjectDeletionSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --safe-project-deletion-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelMoveSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-move-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelDuplicateSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-duplicate-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelRotateSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-rotate-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelMirrorSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-mirror-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelScaleSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-scale-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelAlignSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-align-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.TransformGizmoFoundationSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --transform-gizmo-foundation-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.MoveGizmoSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --move-gizmo-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.RotateGizmoSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --rotate-gizmo-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.ScaleGizmoSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --scale-gizmo-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.TransformGizmoManagerSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --transform-gizmo-manager-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.TransformPanelSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --transform-panel-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.SaveOnExitSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --save-on-exit-smoke-test
    )
    set_tests_properties(
        VoxelForge.Editor.Smoke
        VoxelForge.Editor.VoxelViewportSmoke
        VoxelForge.Editor.VoxelSelectionSmoke
        VoxelForge.Editor.SelectionSystemSmoke
        VoxelForge.Editor.TransformPreviewSmoke
        VoxelForge.Editor.EraseVoxelSmoke
        VoxelForge.Editor.PaintVoxelSmoke
        VoxelForge.Editor.QualityOfLifeSmoke
        VoxelForge.Editor.VoxelSaveSmoke
        VoxelForge.Editor.AddVoxelSmoke
        VoxelForge.Editor.ModelImportSmoke
        VoxelForge.Editor.DragDropImportSmoke
        VoxelForge.Editor.VoxelDocumentSmoke
        VoxelForge.Editor.VoxelRenderSyncSmoke
        VoxelForge.Editor.VoxelRayPickingSmoke
        VoxelForge.Editor.VoxelPencilSmoke
        VoxelForge.Editor.VoxelEraserSmoke
        VoxelForge.Editor.VoxelUndoRedoSmoke
        VoxelForge.Editor.FirstCreationExperienceSmoke
        VoxelForge.Editor.LayoutStabilitySmoke
        VoxelForge.Editor.CreateWorkspaceSmoke
        VoxelForge.Editor.DoubleClickCameraSmoke
        VoxelForge.Editor.ViewportNavigationSmoke
        VoxelForge.Editor.PersistentWorkplaneSmoke
        VoxelForge.Editor.ProjectSessionRestoreSmoke
        VoxelForge.Editor.DirectCreationFlowSmoke
        VoxelForge.Editor.InstantNewModelSmoke
        VoxelForge.Editor.PaletteUiSmoke
        VoxelForge.Editor.VoxelFillSmoke
        VoxelForge.Editor.VoxelBoxSmoke
        VoxelForge.Editor.VoxelLineSmoke
        VoxelForge.Editor.VoxelSphereSmoke
        VoxelForge.Editor.ModernToolbarSmoke
        VoxelForge.Editor.KeyboardShortcutsSmoke
        VoxelForge.Editor.ModernDialogsSmoke
        VoxelForge.Editor.WelcomeScreenSmoke
        VoxelForge.Editor.RemoveRecentProjectSmoke
        VoxelForge.Editor.SafeProjectDeletionSmoke
        VoxelForge.Editor.VoxelMoveSmoke
        VoxelForge.Editor.VoxelDuplicateSmoke
        VoxelForge.Editor.VoxelRotateSmoke
        VoxelForge.Editor.VoxelMirrorSmoke
        VoxelForge.Editor.VoxelScaleSmoke
        VoxelForge.Editor.VoxelAlignSmoke
        VoxelForge.Editor.TransformGizmoFoundationSmoke
        VoxelForge.Editor.MoveGizmoSmoke
        VoxelForge.Editor.RotateGizmoSmoke
        VoxelForge.Editor.ScaleGizmoSmoke
        VoxelForge.Editor.TransformGizmoManagerSmoke
        VoxelForge.Editor.TransformPanelSmoke
        VoxelForge.Editor.SaveOnExitSmoke
        PROPERTIES
        LABELS "EditorApp"
            # L'application retourne 77 (Application::GpuUnavailableExitCode)
            # quand le poste n'a pas de GPU : CTest marque alors le test
            # "Skipped" au lieu d'un echec (runner CI sans device).
            SKIP_RETURN_CODE 77
            TIMEOUT 30
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            ENVIRONMENT
                "VOXELFORGE_RECENT_PROJECTS_FILE=${CMAKE_CURRENT_BINARY_DIR}/smoke-recent-projects.txt;VOXELFORGE_PREFERENCES_FILE=${CMAKE_CURRENT_BINARY_DIR}/smoke-preferences.ini"
    )
endif()

add_executable(
    VoxelForgeEditorVoxelStampDomainTests
    Editor/VoxelStampDomainTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelStampDomainTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorVoxelStampDomainTests)
add_test(
    NAME VoxelForge.Editor.VoxelStampDomain
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelStampDomainTests>
)

add_executable(
    VoxelForgeEditorVfstampFormatTests
    Editor/VfstampFormatTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVfstampFormatTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorVfstampFormatTests)
add_test(
    NAME VoxelForge.Editor.VfstampFormat
    COMMAND $<TARGET_FILE:VoxelForgeEditorVfstampFormatTests>
)

add_executable(
    VoxelForgeEditorVfstampValidationTests
    Editor/VfstampValidationTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVfstampValidationTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorVfstampValidationTests)
add_test(
    NAME VoxelForge.Editor.VfstampValidation
    COMMAND $<TARGET_FILE:VoxelForgeEditorVfstampValidationTests>
)

add_executable(
    VoxelForgeEditorStampCaptureTests
    Editor/StampCaptureTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampCaptureTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorStampCaptureTests)
add_test(
    NAME VoxelForge.Editor.StampCapture
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampCaptureTests>
)

add_executable(
    VoxelForgeEditorStampAutoPivotTests
    Editor/StampAutoPivotTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampAutoPivotTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorStampAutoPivotTests)
add_test(
    NAME VoxelForge.Editor.StampAutoPivot
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampAutoPivotTests>
)

add_executable(
    VoxelForgeEditorStampProjectLibraryTests
    Editor/StampProjectLibraryTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampProjectLibraryTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorStampProjectLibraryTests)
add_test(
    NAME VoxelForge.Editor.StampProjectLibrary
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampProjectLibraryTests>
)

add_executable(
    VoxelForgeEditorStampUserLibraryTests
    Editor/StampUserLibraryTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampUserLibraryTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorStampUserLibraryTests)
add_test(
    NAME VoxelForge.Editor.StampUserLibrary
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampUserLibraryTests>
)

add_executable(
    VoxelForgeEditorStampAssetCacheTests
    Editor/StampAssetCacheTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampAssetCacheTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorStampAssetCacheTests)
add_test(
    NAME VoxelForge.Editor.StampAssetCache
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampAssetCacheTests>
)

add_executable(VoxelForgeEditorStampCatalogTests Editor/StampCatalogTests.cpp)
target_link_libraries(VoxelForgeEditorStampCatalogTests PRIVATE VoxelForgeEditorVoxelStamps)
vf_configure_test(VoxelForgeEditorStampCatalogTests)
add_test(NAME VoxelForge.Editor.StampCatalog COMMAND $<TARGET_FILE:VoxelForgeEditorStampCatalogTests>)

add_executable(
    VoxelForgeEditorSaveSelectionAsStampTests
    Editor/SaveSelectionAsStampTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSaveSelectionAsStampTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorSaveSelectionAsStampTests)
add_test(
    NAME VoxelForge.Editor.SaveSelectionAsStamp
    COMMAND $<TARGET_FILE:VoxelForgeEditorSaveSelectionAsStampTests>
)
add_executable(
    VoxelForgeEditorSaveSelectionAsStampSmokeTests
    Editor/SaveSelectionAsStampSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorSaveSelectionAsStampSmokeTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorSaveSelectionAsStampSmokeTests)
add_test(
    NAME VoxelForge.Editor.SaveSelectionAsStampSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorSaveSelectionAsStampSmokeTests>
)

add_executable(
    VoxelForgeEditorStampLivePreviewTests
    Editor/StampLivePreviewTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampLivePreviewTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorStampLivePreviewTests)
add_test(
    NAME VoxelForge.Editor.StampLivePreview
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampLivePreviewTests>
)

add_executable(
    VoxelForgeEditorStampLivePreviewSmokeTests
    Editor/StampLivePreviewSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampLivePreviewSmokeTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorStampLivePreviewSmokeTests)
add_test(
    NAME VoxelForge.Editor.StampLivePreviewSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampLivePreviewSmokeTests>
)

# PERF-FOUNDATION lot 2: the memoized occupied-palette survey must equal the
# reference scan and only recompute on a real document revision.
add_executable(
    VoxelForgeEditorStampDocumentPaletteCacheTests
    Editor/StampDocumentPaletteCacheTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampDocumentPaletteCacheTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorStampDocumentPaletteCacheTests)
add_test(
    NAME VoxelForge.Editor.StampDocumentPaletteCache
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampDocumentPaletteCacheTests>
)

# STAMP-12: pure deterministic palette planning tests.
add_executable(
    VoxelForgeEditorPaletteMappingEngineTests
    Editor/PaletteMappingEngineTests.cpp
)
target_link_libraries(
    VoxelForgeEditorPaletteMappingEngineTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorPaletteMappingEngineTests)
add_test(
    NAME VoxelForge.Editor.PaletteMappingEngine
    COMMAND $<TARGET_FILE:VoxelForgeEditorPaletteMappingEngineTests>
)

add_executable(
    VoxelForgeEditorPaletteMappingEngineSmokeTests
    Editor/PaletteMappingEngineSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorPaletteMappingEngineSmokeTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorPaletteMappingEngineSmokeTests)
add_test(
    NAME VoxelForge.Editor.PaletteMappingEngineSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorPaletteMappingEngineSmokeTests>
)

# STAMP-13: placement preparation uses the existing preview and composite history operation.
add_executable(
    VoxelForgeEditorPlaceVoxelStampOperationTests
    Editor/PlaceVoxelStampOperationTests.cpp
)
target_link_libraries(
    VoxelForgeEditorPlaceVoxelStampOperationTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorPlaceVoxelStampOperationTests)
add_test(
    NAME VoxelForge.Editor.PlaceVoxelStampOperation
    COMMAND $<TARGET_FILE:VoxelForgeEditorPlaceVoxelStampOperationTests>
)

add_executable(
    VoxelForgeEditorPlaceVoxelStampOperationSmokeTests
    Editor/StampPlacementSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorPlaceVoxelStampOperationSmokeTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorPlaceVoxelStampOperationSmokeTests)
add_test(
    NAME VoxelForge.Editor.PlaceVoxelStampOperationSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorPlaceVoxelStampOperationSmokeTests>
)

# STAMP-16: the active session owns continuous placement, ordinal advancement,
# stale-preview confirmation and document-switch cancellation.
add_executable(
    VoxelForgeEditorStampPlacementSessionTests
    Editor/StampPlacementSessionTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampPlacementSessionTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorStampPlacementSessionTests)
add_test(
    NAME VoxelForge.Editor.StampPlacementSession
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampPlacementSessionTests>
)

# STAMP-14: one immutable plan is shared by preview and placement.
add_executable(
    VoxelForgeEditorStampPlacementPlanningTests
    Editor/StampPlacementPlanningTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampPlacementPlanningTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorStampPlacementPlanningTests)
add_test(
    NAME VoxelForge.Editor.StampPlacementPlanning
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampPlacementPlanningTests>
)

add_executable(
    VoxelForgeEditorStampPlacementPlanningSmokeTests
    Editor/StampPlacementPlanningSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampPlacementPlanningSmokeTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorStampPlacementPlanningSmokeTests)
add_test(
    NAME VoxelForge.Editor.StampPlacementPlanningSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampPlacementPlanningSmokeTests>
)

# STAMP-15: exact pivot-aware quarter turns are resolved by the planner.
add_executable(
    VoxelForgeEditorStampQuarterRotationTests
    Editor/StampQuarterRotationTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampQuarterRotationTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorStampQuarterRotationTests)
add_test(
    NAME VoxelForge.Editor.StampQuarterRotation
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampQuarterRotationTests>
)

add_executable(
    VoxelForgeEditorStampQuarterRotationSmokeTests
    Editor/StampQuarterRotationSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampQuarterRotationSmokeTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorStampQuarterRotationSmokeTests)
add_test(
    NAME VoxelForge.Editor.StampQuarterRotationSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampQuarterRotationSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.StampQuarterRotationSmoke
    PROPERTIES LABELS "Smoke"
)

# STAMP-17: exact X/Z mirrors are resolved before quarter rotation.
add_executable(
    VoxelForgeEditorStampMirrorTests
    Editor/StampMirrorTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampMirrorTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorStampMirrorTests)
add_test(
    NAME VoxelForge.Editor.StampMirror
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampMirrorTests>
)

add_executable(
    VoxelForgeEditorStampMirrorSmokeTests
    Editor/StampMirrorSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampMirrorSmokeTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorStampMirrorSmokeTests)
add_test(
    NAME VoxelForge.Editor.StampMirrorSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampMirrorSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.StampMirrorSmoke
    PROPERTIES LABELS "Smoke"
)

# STAMP-18: the Project Forge Library selects catalogue entries and activates
# the existing placement session without owning any placement calculations.
add_executable(
    VoxelForgeEditorForgeLibraryViewModelTests
    Editor/ForgeLibraryViewModelTests.cpp
)
target_link_libraries(
    VoxelForgeEditorForgeLibraryViewModelTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorForgeLibraryViewModelTests)
add_test(
    NAME VoxelForge.Editor.ForgeLibrary
    COMMAND $<TARGET_FILE:VoxelForgeEditorForgeLibraryViewModelTests>
)

add_executable(
    VoxelForgeEditorForgeLibrarySmokeTests
    Editor/ForgeLibrarySmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorForgeLibrarySmokeTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands VoxelForgeMesh
)
vf_configure_test(VoxelForgeEditorForgeLibrarySmokeTests)
add_test(
    NAME VoxelForge.Editor.ForgeLibrarySmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorForgeLibrarySmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.ForgeLibrarySmoke
    PROPERTIES LABELS "Smoke"
)

# STAMP-19: Smart Variant resolution is a pure deterministic domain service.
add_executable(
    VoxelForgeEditorStampVariantTests
    Editor/StampVariantTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampVariantTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorStampVariantTests)
add_test(
    NAME VoxelForge.Editor.StampVariant
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampVariantTests>
)

# STAMP-20: resolved variant identity is retained through atomic placement and
# source-independent Undo/Redo without rerolling.
add_executable(
    VoxelForgeEditorStampVariantPlacementTests
    Editor/StampVariantPlacementTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampVariantPlacementTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorStampVariantPlacementTests)
add_test(
    NAME VoxelForge.Editor.StampVariantPlacement
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampVariantPlacementTests>
)
set_tests_properties(
    VoxelForge.Editor.StampVariantPlacement
    PROPERTIES LABELS "Smoke"
)

# STAMP-21: Smart Placement remains deterministic, enabled by default,
# bypassable and advisory when a suggested preview would become invalid.
add_executable(
    VoxelForgeEditorStampSmartPlacementTests
    Editor/StampSmartPlacementTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampSmartPlacementTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorStampSmartPlacementTests)
add_test(
    NAME VoxelForge.Editor.StampSmartPlacement
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampSmartPlacementTests>
)
set_tests_properties(
    VoxelForge.Editor.StampSmartPlacement
    PROPERTIES LABELS "Smoke"
)

# STAMP-22: resource preflights, bounded caches, catalogue query isolation,
# realistic placement densities and stable unchanged previews.
add_executable(
    VoxelForgeEditorStampPerformanceTests
    Editor/StampPerformanceTests.cpp
)
target_link_libraries(
    VoxelForgeEditorStampPerformanceTests
    PRIVATE VoxelForgeEditorVoxelStamps VoxelForgeEditorCommands
)
vf_configure_test(VoxelForgeEditorStampPerformanceTests)
add_test(
    NAME VoxelForge.Editor.StampPerformance
    COMMAND $<TARGET_FILE:VoxelForgeEditorStampPerformanceTests>
)
set_tests_properties(
    VoxelForge.Editor.StampPerformance
    PROPERTIES LABELS "Performance"
)

# STAMP-23: one aggregate gate plus the public, headless editor diagnostic
# switches. Every scenario injects and removes its own project/profile roots.
add_executable(
    VoxelForgeEditorVoxelStampSmokeTests
    Editor/VoxelStampSmokeTests.cpp
)
target_link_libraries(
    VoxelForgeEditorVoxelStampSmokeTests
    PRIVATE VoxelForgeEditorVoxelStamps
)
vf_configure_test(VoxelForgeEditorVoxelStampSmokeTests)
add_test(
    NAME VoxelForge.Editor.VoxelStampReleaseSmoke
    COMMAND $<TARGET_FILE:VoxelForgeEditorVoxelStampSmokeTests>
)
set_tests_properties(
    VoxelForge.Editor.VoxelStampReleaseSmoke
    PROPERTIES
        LABELS "Smoke;StampRelease"
        TIMEOUT 30
)

if(TARGET VoxelForgeEditor)
    add_test(
        NAME VoxelForge.Editor.VoxelStampMvpSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-stamp-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelStampCorruptionSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-stamp-corruption-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelStampLibrarySmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-stamp-library-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelStampVariantSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-stamp-variant-smoke-test
    )
    add_test(
        NAME VoxelForge.Editor.VoxelStampSmartPlacementSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --voxel-stamp-smart-placement-smoke-test
    )
    set_tests_properties(
        VoxelForge.Editor.VoxelStampMvpSmoke
        VoxelForge.Editor.VoxelStampCorruptionSmoke
        VoxelForge.Editor.VoxelStampLibrarySmoke
        VoxelForge.Editor.VoxelStampVariantSmoke
        VoxelForge.Editor.VoxelStampSmartPlacementSmoke
        PROPERTIES
            LABELS "Smoke;StampRelease"
            TIMEOUT 30
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
endif()
