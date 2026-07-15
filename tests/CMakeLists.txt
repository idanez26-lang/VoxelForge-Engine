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

set_tests_properties(
    VoxelForge.Core.Events
    VoxelForge.Core.Layers
    VoxelForge.Editor.ExitRequest
    VoxelForge.Editor.WindowTitle
    VoxelForge.Editor.AssetBrowser
    VoxelForge.Editor.VoxelViewport
    VoxelForge.Editor.VoxelSelection
    VoxelForge.Editor.CommandHistory
    VoxelForge.Project.Manager
    VoxelForge.Asset.VoxImporter
    VoxelForge.Voxel.Core
    VoxelForge.Mesh.VoxelMeshBuilder
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
    set_tests_properties(
        VoxelForge.Editor.Smoke
        VoxelForge.Editor.VoxelViewportSmoke
        VoxelForge.Editor.VoxelSelectionSmoke
        PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            ENVIRONMENT
                "VOXELFORGE_RECENT_PROJECTS_FILE=${CMAKE_CURRENT_BINARY_DIR}/smoke-recent-projects.txt"
    )
endif()
