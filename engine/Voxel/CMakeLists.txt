add_library(VoxelForgeVoxel STATIC)

target_sources(
    VoxelForgeVoxel
    PRIVATE
        src/VoxelGrid.cpp
        src/VoxelModel.cpp
        src/VoxelModelSerializer.cpp
        src/VoxelPalette.cpp
)

target_include_directories(
    VoxelForgeVoxel
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(VoxelForgeVoxel PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(VoxelForgeVoxel PRIVATE /W4 /permissive-)
else()
    target_compile_options(VoxelForgeVoxel PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(VoxelForgeVoxConversion STATIC)

target_sources(
    VoxelForgeVoxConversion
    PRIVATE
        src/VoxModelConverter.cpp
)

target_include_directories(
    VoxelForgeVoxConversion
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    VoxelForgeVoxConversion
    PUBLIC
        VoxelForgeVoxel
        VoxelForgeAsset
)

target_compile_features(VoxelForgeVoxConversion PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(VoxelForgeVoxConversion PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        VoxelForgeVoxConversion
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()
