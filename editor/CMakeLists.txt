add_library(VoxelForgeEditorAssetMetadata STATIC)
target_sources(
    VoxelForgeEditorAssetMetadata
    PRIVATE
        src/ModelImport/ModelAssetMetadataService.cpp
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
    PUBLIC VoxelForgeAsset VoxelForgeRenderer SDL3::SDL3)
if(MSVC)
    target_compile_options(VoxelForgeEditorAssetMetadata PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorAssetMetadata PRIVATE -Wall -Wextra -Wpedantic)
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
        src/VoxelSelection/VoxelRaycast.cpp
        src/VoxelSelection/VoxelSelectionState.cpp
)
target_include_directories(
    VoxelForgeEditorVoxelSelection
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(VoxelForgeEditorVoxelSelection PUBLIC VoxelForgeVoxel)
target_compile_features(VoxelForgeEditorVoxelSelection PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        VoxelForgeEditorVoxelSelection PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeEditorVoxelSelection PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
)
target_include_directories(
    VoxelForgeEditorCommands
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(VoxelForgeEditorCommands PUBLIC cxx_std_20)
target_link_libraries(
    VoxelForgeEditorCommands
    PUBLIC VoxelForgeVoxel VoxelForgeAsset)
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
        src/ViewportRenderer.cpp
        src/VoxelViewportState.cpp
        src/main.cpp
)
target_include_directories(VoxelForgeEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(
    VoxelForgeEditor
    PRIVATE
        VoxelForgeEditorAssetBrowser
        VoxelForgeEditorAssetInspector
        VoxelForgeEditorCommands
        VoxelForgeEditorModelImport
        VoxelForgeEditorQualityOfLife
        VoxelForgeEditorVoxelSelection
        VoxelForgeEditorVoxelDocument
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
