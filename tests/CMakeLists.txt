function(vf_configure_test target)
    target_compile_features(${target} PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

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

set_tests_properties(
    VoxelForge.Core.Events
    VoxelForge.Core.Layers
    VoxelForge.Editor.ExitRequest
    VoxelForge.Editor.WindowTitle
    VoxelForge.Editor.AssetBrowser
    VoxelForge.Editor.ModelImport
    VoxelForge.Editor.DragDropImport
    VoxelForge.Editor.AssetMetadata
    VoxelForge.Editor.AssetInspector
    VoxelForge.Editor.VoxThumbnail
    VoxelForge.Editor.VoxelViewport
    VoxelForge.Editor.DoubleClickCamera
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
        NAME VoxelForge.Editor.DoubleClickCameraSmoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --double-click-camera-smoke-test
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
    set_tests_properties(
        VoxelForge.Editor.Smoke
        VoxelForge.Editor.VoxelViewportSmoke
        VoxelForge.Editor.VoxelSelectionSmoke
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
        VoxelForge.Editor.DoubleClickCameraSmoke
        VoxelForge.Editor.PersistentWorkplaneSmoke
        VoxelForge.Editor.ProjectSessionRestoreSmoke
        VoxelForge.Editor.DirectCreationFlowSmoke
        VoxelForge.Editor.PaletteUiSmoke
        VoxelForge.Editor.VoxelFillSmoke
        VoxelForge.Editor.VoxelBoxSmoke
        VoxelForge.Editor.VoxelLineSmoke
        PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            ENVIRONMENT
                "VOXELFORGE_RECENT_PROJECTS_FILE=${CMAKE_CURRENT_BINARY_DIR}/smoke-recent-projects.txt;VOXELFORGE_PREFERENCES_FILE=${CMAKE_CURRENT_BINARY_DIR}/smoke-preferences.ini"
    )
endif()
