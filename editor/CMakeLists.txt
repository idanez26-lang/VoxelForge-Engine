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
    PRIVATE
        VoxelForgeAsset
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
