add_executable(VoxelForgeEditor)
target_sources(
    VoxelForgeEditor
    PRIVATE
        src/EditorCamera.cpp
        src/EditorLayer.cpp
        src/EditorWorkspace.cpp
        src/ViewportRenderer.cpp
        src/main.cpp
)
target_include_directories(VoxelForgeEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VoxelForgeEditor PRIVATE VoxelForgeProject VoxelForgeRenderer VoxelForgeScene VoxelForgeCore VoxelForgeImGui)
target_compile_features(VoxelForgeEditor PRIVATE cxx_std_20)
if(MSVC)
 target_compile_options(VoxelForgeEditor PRIVATE /W4 /permissive-)
else()
 target_compile_options(VoxelForgeEditor PRIVATE -Wall -Wextra -Wpedantic)
endif()
