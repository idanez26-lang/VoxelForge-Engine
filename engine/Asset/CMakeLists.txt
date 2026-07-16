add_library(VoxelForgeAsset STATIC)

target_sources(
    VoxelForgeAsset
    PRIVATE
        src/Vox/VoxFormat.cpp
        src/Vox/VoxImporter.cpp
        src/Vox/VoxModelAnalyzer.cpp
        src/Voxel/VoxelDocument.cpp
        src/Voxel/VoxDocumentLoader.cpp
)

target_include_directories(
    VoxelForgeAsset
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(VoxelForgeAsset PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(VoxelForgeAsset PRIVATE /W4 /permissive-)
else()
    target_compile_options(VoxelForgeAsset PRIVATE -Wall -Wextra -Wpedantic)
endif()
