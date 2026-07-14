add_library(VoxelForgeScene STATIC)

target_sources(
    VoxelForgeScene
    PRIVATE
        src/Entity.cpp
        src/Scene.cpp
)

target_include_directories(
    VoxelForgeScene
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    VoxelForgeScene
    PUBLIC
        VoxelForgeCore
)

target_compile_features(VoxelForgeScene PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(VoxelForgeScene PRIVATE /W4 /permissive-)
else()
    target_compile_options(VoxelForgeScene PRIVATE -Wall -Wextra -Wpedantic)
endif()
