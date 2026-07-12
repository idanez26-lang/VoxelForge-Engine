add_executable(VoxelForgeEditor)

target_sources(
    VoxelForgeEditor
    PRIVATE
        src/main.cpp
)

target_link_libraries(
    VoxelForgeEditor
    PRIVATE
        VoxelForgeCore
)

target_compile_features(VoxelForgeEditor PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(VoxelForgeEditor PRIVATE /W4 /permissive-)
else()
    target_compile_options(VoxelForgeEditor PRIVATE -Wall -Wextra -Wpedantic)
endif()
