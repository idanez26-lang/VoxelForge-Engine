add_library(VoxelForgeMesh STATIC)

target_sources(
    VoxelForgeMesh
    PRIVATE
        src/VoxelDocumentMeshCache.cpp
        src/VoxelMeshBuilder.cpp
)

target_include_directories(
    VoxelForgeMesh
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    VoxelForgeMesh
    PUBLIC
        VoxelForgeVoxel
        VoxelForgeAsset
)

target_compile_features(VoxelForgeMesh PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(VoxelForgeMesh PRIVATE /W4 /permissive-)
else()
    target_compile_options(VoxelForgeMesh PRIVATE -Wall -Wextra -Wpedantic)
endif()
