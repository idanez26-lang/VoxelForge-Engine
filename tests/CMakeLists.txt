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
    VoxelForgeProjectManagerTests
    Project/ProjectManagerTests.cpp
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

set_tests_properties(
    VoxelForge.Core.Events
    VoxelForge.Core.Layers
    VoxelForge.Editor.ExitRequest
    VoxelForge.Project.Manager
    PROPERTIES TIMEOUT 10
)

if(TARGET VoxelForgeEditor)
    add_test(
        NAME VoxelForge.Editor.Smoke
        COMMAND $<TARGET_FILE:VoxelForgeEditor> --smoke-test
    )
    set_tests_properties(
        VoxelForge.Editor.Smoke
        PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
endif()
