add_library(VoxelForgeEditorAssetMetadataCore STATIC)
target_sources(
    VoxelForgeEditorAssetMetadataCore
    PRIVATE
        src/ModelImport/ModelAssetMetadataService.cpp
)
target_include_directories(
    VoxelForgeEditorAssetMetadataCore
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(VoxelForgeEditorAssetMetadataCore PUBLIC cxx_std_20)
target_link_libraries(
    VoxelForgeEditorAssetMetadataCore
    PUBLIC VoxelForgeAsset)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorAssetMetadataCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorAssetMetadataCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorAssetMetadata STATIC)
target_sources(
    VoxelForgeEditorAssetMetadata
    PRIVATE
        src/Thumbnail/ThumbnailImage.cpp
        src/Thumbnail/ThumbnailTextureCache.cpp
        src/Thumbnail/VoxThumbnailRenderer.cpp
        src/Thumbnail/VoxThumbnailService.cpp
)
target_include_directories(
    VoxelForgeEditorAssetMetadata
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(VoxelForgeEditorAssetMetadata PUBLIC cxx_std_20)
target_link_libraries(
    VoxelForgeEditorAssetMetadata
    PUBLIC
        VoxelForgeEditorAssetMetadataCore
        VoxelForgeRenderer
        SDL3::SDL3)
if(MSVC)
    target_compile_options(VoxelForgeEditorAssetMetadata PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorAssetMetadata PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorDialogs STATIC)
target_sources(
    VoxelForgeEditorDialogs
    PRIVATE
        src/Dialogs/EditorDialogModel.cpp
        src/Dialogs/EditorDialogStyle.cpp
)
target_include_directories(
    VoxelForgeEditorDialogs
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(VoxelForgeEditorDialogs PRIVATE VoxelForgeImGui)
target_compile_features(VoxelForgeEditorDialogs PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(VoxelForgeEditorDialogs PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorDialogs PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorAssetBrowser STATIC)
target_sources(
    VoxelForgeEditorAssetBrowser
    PRIVATE
        src/AssetBrowser/AssetBrowser.cpp
        src/AssetBrowser/AssetBrowserViewModel.cpp
        src/AssetBrowser/AssetDirectory.cpp
        src/AssetBrowser/AssetEntry.cpp
)
target_include_directories(
    VoxelForgeEditorAssetBrowser
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    VoxelForgeEditorAssetBrowser
    PUBLIC
        VoxelForgeAsset
        VoxelForgeEditorAssetMetadata
    PRIVATE
        VoxelForgeEditorDialogs
        VoxelForgeImGui
)
target_compile_features(VoxelForgeEditorAssetBrowser PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorAssetBrowser
        PRIVATE
            /W4
            /permissive-
    )
else()
    target_compile_options(
        VoxelForgeEditorAssetBrowser
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_library(VoxelForgeEditorAssetInspector STATIC)
target_sources(
    VoxelForgeEditorAssetInspector
    PRIVATE src/AssetInspector/AssetInspectorViewModel.cpp
)
target_include_directories(
    VoxelForgeEditorAssetInspector
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    VoxelForgeEditorAssetInspector
    PUBLIC
        VoxelForgeEditorAssetBrowser
        VoxelForgeEditorAssetMetadata
)
target_compile_features(VoxelForgeEditorAssetInspector PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(VoxelForgeEditorAssetInspector PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorAssetInspector PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorVoxelSelection STATIC)
target_sources(
    VoxelForgeEditorVoxelSelection
    PRIVATE
        src/VoxelSelection/ViewportRayBuilder.cpp
        src/VoxelSelection/VoxelRaycast.cpp
        src/VoxelSelection/VoxelSelectionState.cpp
        src/Selection/SelectionService.cpp
        src/Selection/SelectionInteraction.cpp
        src/Selection/SelectionHandleModel.cpp
        src/Selection/SelectionBoxMoveModel.cpp
        src/Selection/SelectionVolumeCache.cpp
        src/Selection/SelectionHighlightPolicy.cpp
)
target_include_directories(
    VoxelForgeEditorVoxelSelection
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    VoxelForgeEditorVoxelSelection PUBLIC VoxelForgeVoxel VoxelForgeAsset)
target_compile_features(VoxelForgeEditorVoxelSelection PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorVoxelSelection PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorVoxelSelection PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorTransformPreview STATIC)
target_sources(
    VoxelForgeEditorTransformPreview
    PRIVATE src/Transform/TransformPreviewModel.cpp
)
target_include_directories(
    VoxelForgeEditorTransformPreview
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    VoxelForgeEditorTransformPreview
    PUBLIC VoxelForgeEditorVoxelSelection VoxelForgeAsset
)
target_compile_features(VoxelForgeEditorTransformPreview PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorTransformPreview PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorTransformPreview PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorConstraints STATIC)
target_sources(
    VoxelForgeEditorConstraints
    PRIVATE
        src/Constraints/ConstraintEngine.cpp
        src/Constraints/Constraints/GridConstraint.cpp
        src/Constraints/Constraints/RotationConstraint.cpp
)
target_include_directories(
    VoxelForgeEditorConstraints
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(VoxelForgeEditorConstraints PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorConstraints PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorConstraints PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorTransformGizmo STATIC)
target_sources(
    VoxelForgeEditorTransformGizmo
    PRIVATE
        src/TransformGizmo/TransformGizmoModel.cpp
        src/TransformGizmo/TransformGizmoInteraction.cpp
        src/TransformGizmo/TransformGizmoManager.cpp
)
target_include_directories(
    VoxelForgeEditorTransformGizmo
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    VoxelForgeEditorTransformGizmo
    PUBLIC VoxelForgeEditorVoxelSelection
)
target_compile_features(VoxelForgeEditorTransformGizmo PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorTransformGizmo PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorTransformGizmo PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorTransformPivot STATIC)
target_sources(
    VoxelForgeEditorTransformPivot
    PRIVATE src/Transform/TransformPivotManager.cpp
)
target_include_directories(
    VoxelForgeEditorTransformPivot
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    VoxelForgeEditorTransformPivot
    PUBLIC VoxelForgeEditorVoxelSelection
)
target_compile_features(VoxelForgeEditorTransformPivot PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorTransformPivot PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorTransformPivot PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(
    VoxelForgeEditorTransformGizmo
    PUBLIC VoxelForgeEditorTransformPivot
)

add_library(VoxelForgeEditorVoxelDocument STATIC)
target_sources(
    VoxelForgeEditorVoxelDocument
    PRIVATE src/VoxelDocument/VoxelDocumentSession.cpp
)
target_include_directories(
    VoxelForgeEditorVoxelDocument
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    VoxelForgeEditorVoxelDocument
    PUBLIC VoxelForgeAsset
)
target_compile_features(VoxelForgeEditorVoxelDocument PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorVoxelDocument PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorVoxelDocument PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorPalette STATIC)
target_sources(
    VoxelForgeEditorPalette
    PRIVATE src/Palette/PaletteService.cpp
)
target_include_directories(
    VoxelForgeEditorPalette
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(VoxelForgeEditorPalette PUBLIC VoxelForgeAsset)
target_compile_features(VoxelForgeEditorPalette PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(VoxelForgeEditorPalette PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorPalette PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorCommands STATIC)
target_sources(
    VoxelForgeEditorCommands
    PRIVATE
        src/Commands/CommandHistory.cpp
        src/Commands/Voxel/AddVoxelCommand.cpp
        src/Commands/Voxel/AddVoxelTarget.cpp
        src/Commands/Voxel/EraseVoxelCommand.cpp
        src/Commands/Voxel/PaintVoxelCommand.cpp
        src/Commands/Voxel/VoxelEditTransaction.cpp
        src/Transform/AlignVoxelSelectionOperation.cpp
        src/Transform/DuplicateVoxelSelectionOperation.cpp
        src/Transform/MoveVoxelSelectionOperation.cpp
        src/Transform/MirrorVoxelSelectionOperation.cpp
        src/Transform/RotateVoxelSelectionOperation.cpp
        src/Transform/ScaleVoxelSelectionOperation.cpp
        src/Transform/TransformOperationFramework.cpp
        src/VoxelHistory/VoxelEditHistory.cpp
        src/VoxelHistory/VoxelEditOperation.cpp
        src/VoxelHistory/VoxelHistoryInput.cpp
        src/BrushEngine/SmartBrushEngine.cpp
        src/SmartTools/SmartTool.cpp
        src/SmartTools/SmartBrushPreviewResolver.cpp
        src/VoxelTools/VoxelEraserTool.cpp
        src/VoxelTools/VoxelBoxService.cpp
        src/VoxelTools/VoxelBrush.cpp
        src/VoxelTools/VoxelFillService.cpp
        src/VoxelTools/VoxelPaintBrushTool.cpp
        src/VoxelTools/VoxelLineService.cpp
        src/VoxelTools/VoxelSphereService.cpp
        src/VoxelTools/VoxelPencilInput.cpp
        src/VoxelTools/VoxelPencilPreview.cpp
        src/VoxelTools/VoxelPencilTool.cpp
        src/VoxelTools/VoxelToolState.cpp
)
target_include_directories(
    VoxelForgeEditorCommands
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(VoxelForgeEditorCommands PUBLIC cxx_std_20)
target_link_libraries(
    VoxelForgeEditorCommands
    PUBLIC
        VoxelForgeVoxel
        VoxelForgeAsset
        VoxelForgeEditorVoxelSelection
        VoxelForgeEditorTransformPreview)
if(MSVC)
    target_compile_options(VoxelForgeEditorCommands PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorCommands PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorQualityOfLife STATIC)
target_sources(
    VoxelForgeEditorQualityOfLife
    PRIVATE
        src/Platform/SDLFileDialogService.cpp
        src/Platform/SDLProjectFolderOpener.cpp
        src/Project/ProjectDialogPreferences.cpp
)
target_include_directories(
    VoxelForgeEditorQualityOfLife
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(VoxelForgeEditorQualityOfLife PUBLIC SDL3::SDL3)
target_compile_features(VoxelForgeEditorQualityOfLife PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorQualityOfLife PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorQualityOfLife PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorTransformPanel STATIC)
target_sources(
    VoxelForgeEditorTransformPanel
    PRIVATE src/TransformPanel/TransformPanelViewModel.cpp
)
target_include_directories(
    VoxelForgeEditorTransformPanel
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    VoxelForgeEditorTransformPanel
    PUBLIC
        VoxelForgeEditorCommands
        VoxelForgeEditorTransformPivot
)
target_compile_features(VoxelForgeEditorTransformPanel PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorTransformPanel PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorTransformPanel PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorLayout STATIC)
target_sources(
    VoxelForgeEditorLayout
    PRIVATE
        src/Layout/EditorLayoutPersistence.cpp
        src/Layout/InspectorLayoutModel.cpp
        src/Layout/PalettePanelLayout.cpp
)
target_include_directories(
    VoxelForgeEditorLayout
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    VoxelForgeEditorLayout
    PUBLIC VoxelForgeEditorCommands)
target_compile_features(VoxelForgeEditorLayout PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(VoxelForgeEditorLayout PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorLayout PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorProjectSession STATIC)
target_sources(
    VoxelForgeEditorProjectSession
    PRIVATE src/ProjectSession/ProjectSessionService.cpp
)
target_include_directories(
    VoxelForgeEditorProjectSession
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(VoxelForgeEditorProjectSession PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorProjectSession PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorProjectSession PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorWelcome STATIC)
target_sources(
    VoxelForgeEditorWelcome
    PRIVATE
        src/Welcome/ProjectDeletionService.cpp
        src/Welcome/WelcomeScreenModel.cpp
)
target_include_directories(
    VoxelForgeEditorWelcome
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    VoxelForgeEditorWelcome
    PUBLIC VoxelForgeProject
)
if(WIN32)
    target_link_libraries(VoxelForgeEditorWelcome PRIVATE Ole32 Shell32)
endif()
target_compile_features(VoxelForgeEditorWelcome PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(VoxelForgeEditorWelcome PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorWelcome PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorModelImport STATIC)
target_sources(
    VoxelForgeEditorModelImport
    PRIVATE
        src/DragDropImport/DragDropImportController.cpp
        src/ModelImport/ModelImportService.cpp
)
target_link_libraries(
    VoxelForgeEditorModelImport
    PUBLIC VoxelForgeEditorAssetMetadata)
target_include_directories(
    VoxelForgeEditorModelImport
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(VoxelForgeEditorModelImport PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorModelImport PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorModelImport PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorVoxelSave STATIC)
target_sources(
    VoxelForgeEditorVoxelSave
    PRIVATE src/VoxelSave/VoxelDocumentSaveService.cpp
)
target_include_directories(
    VoxelForgeEditorVoxelSave
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    VoxelForgeEditorVoxelSave
    PUBLIC
        VoxelForgeAsset
        VoxelForgeEditorAssetMetadata
        VoxelForgeEditorCommands
)
target_compile_features(VoxelForgeEditorVoxelSave PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(VoxelForgeEditorVoxelSave PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorVoxelSave PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeEditorVoxelCreation STATIC)
target_sources(
    VoxelForgeEditorVoxelCreation
    PRIVATE
        src/VoxelCreation/DirectCreationFlowService.cpp
        src/VoxelCreation/FirstCreationExperience.cpp
        src/VoxelCreation/NewVoxelModelWorkflow.cpp
        src/VoxelCreation/VoxelConstructionPlane.cpp
        src/VoxelCreation/VoxelModelCreationService.cpp
        src/VoxelCreation/WorkplaneService.cpp
)
target_include_directories(
    VoxelForgeEditorVoxelCreation
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(
    VoxelForgeEditorVoxelCreation
    PUBLIC
        VoxelForgeAsset
        VoxelForgeEditorAssetMetadataCore
        VoxelForgeEditorVoxelSelection
)
target_compile_features(VoxelForgeEditorVoxelCreation PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(VoxelForgeEditorVoxelCreation PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorVoxelCreation PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(VoxelForgeEditor)

if(WIN32)
    cmake_host_system_information(
        RESULT VOXELFORGE_WINDOWS_SDK_ROOT
        QUERY WINDOWS_REGISTRY
            "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots"
        VALUE KitsRoot10
        VIEW 32_64
    )
    file(
        GLOB VOXELFORGE_DXC_CANDIDATES
        "${VOXELFORGE_WINDOWS_SDK_ROOT}/bin/*/x64/dxc.exe"
    )
    if(NOT VOXELFORGE_DXC_CANDIDATES)
        message(
            FATAL_ERROR
            "DXC from the Windows SDK is required for the voxel viewport shaders."
        )
    endif()
    list(SORT VOXELFORGE_DXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
    list(GET VOXELFORGE_DXC_CANDIDATES 0 VOXELFORGE_DXC_EXECUTABLE)

    set(VOXELFORGE_VIEWPORT_SHADER_DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    set(VOXELFORGE_VIEWPORT_VERTEX_SHADER
        "${VOXELFORGE_VIEWPORT_SHADER_DIRECTORY}/VoxelViewport.vert.dxil")
    set(VOXELFORGE_VIEWPORT_FRAGMENT_SHADER
        "${VOXELFORGE_VIEWPORT_SHADER_DIRECTORY}/VoxelViewport.frag.dxil")

    add_custom_command(
        OUTPUT ${VOXELFORGE_VIEWPORT_VERTEX_SHADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${VOXELFORGE_VIEWPORT_SHADER_DIRECTORY}
        COMMAND ${VOXELFORGE_DXC_EXECUTABLE}
            -T vs_6_0 -E main -Fo ${VOXELFORGE_VIEWPORT_VERTEX_SHADER}
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/VoxelViewport.vert.hlsl
        DEPENDS shaders/VoxelViewport.vert.hlsl
        VERBATIM
    )
    add_custom_command(
        OUTPUT ${VOXELFORGE_VIEWPORT_FRAGMENT_SHADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${VOXELFORGE_VIEWPORT_SHADER_DIRECTORY}
        COMMAND ${VOXELFORGE_DXC_EXECUTABLE}
            -T ps_6_0 -E main -Fo ${VOXELFORGE_VIEWPORT_FRAGMENT_SHADER}
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/VoxelViewport.frag.hlsl
        DEPENDS shaders/VoxelViewport.frag.hlsl
        VERBATIM
    )
    add_custom_target(
        VoxelForgeViewportShaders
        DEPENDS
            ${VOXELFORGE_VIEWPORT_VERTEX_SHADER}
            ${VOXELFORGE_VIEWPORT_FRAGMENT_SHADER}
    )
    add_dependencies(VoxelForgeEditor VoxelForgeViewportShaders)
    target_compile_definitions(
        VoxelForgeEditor
        PRIVATE
            VOXELFORGE_VIEWPORT_VERTEX_SHADER="${VOXELFORGE_VIEWPORT_VERTEX_SHADER}"
            VOXELFORGE_VIEWPORT_FRAGMENT_SHADER="${VOXELFORGE_VIEWPORT_FRAGMENT_SHADER}"
    )
else()
    target_compile_definitions(
        VoxelForgeEditor
        PRIVATE
            VOXELFORGE_VIEWPORT_VERTEX_SHADER=""
            VOXELFORGE_VIEWPORT_FRAGMENT_SHADER=""
    )
endif()

target_sources(
    VoxelForgeEditor
    PRIVATE
        src/EditorCamera.cpp
        src/EditorLayer.cpp
        src/EditorWorkspace.cpp
        src/Input/EditorInputService.cpp
        src/Tools/ToolManager.cpp
        src/Tools/ToolPanel.cpp
        src/Tools/Panels/SmartBrushOptions.cpp
        src/Tools/Panels/SmartToolPanel.cpp
        src/Tools/Panels/MovePanel.cpp
        src/Tools/Panels/RotatePanel.cpp
        src/Tools/Panels/ScalePanel.cpp
        src/Toolbar/EditorToolbar.cpp
        src/Toolbar/EditorToolbarModel.cpp
        src/ViewportInput/ViewportCameraInput.cpp
        src/ViewportNavigationController.cpp
        src/ViewportRenderer.cpp
        src/VoxelViewportState.cpp
        src/main.cpp
)
target_include_directories(VoxelForgeEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(
    VoxelForgeEditor
    PRIVATE
        VoxelForgeEditorAssetBrowser
        VoxelForgeEditorDialogs
        VoxelForgeEditorAssetInspector
        VoxelForgeEditorCommands
        VoxelForgeEditorConstraints
        VoxelForgeEditorModelImport
        VoxelForgeEditorQualityOfLife
        VoxelForgeEditorLayout
        VoxelForgeEditorProjectSession
        VoxelForgeEditorPalette
        VoxelForgeEditorTransformGizmo
        VoxelForgeEditorTransformPanel
        VoxelForgeEditorTransformPivot
        VoxelForgeEditorTransformPreview
        VoxelForgeEditorVoxelSelection
        VoxelForgeEditorVoxelDocument
        VoxelForgeEditorVoxelCreation
        VoxelForgeEditorVoxelSave
        VoxelForgeEditorWelcome
        VoxelForgeProject
        VoxelForgeRenderer
        VoxelForgeScene
        VoxelForgeCore
        VoxelForgeImGui
        VoxelForgeMesh
        VoxelForgeVoxConversion
)
target_compile_features(VoxelForgeEditor PRIVATE cxx_std_20)
if(MSVC)
 target_compile_options(VoxelForgeEditor PRIVATE /W4 /permissive-)
else()
 target_compile_options(VoxelForgeEditor PRIVATE -Wall -Wextra -Wpedantic)
endif()
