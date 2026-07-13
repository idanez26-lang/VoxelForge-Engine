add_library(VoxelForgeProject STATIC)

target_sources(
    VoxelForgeProject
    PRIVATE
        src/Project.cpp
        src/ProjectManager.cpp
)

target_include_directories(
    VoxelForgeProject
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    VoxelForgeProject
    PUBLIC
        VoxelForgeCore
)

target_compile_features(VoxelForgeProject PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(VoxelForgeProject PRIVATE /W4 /permissive-)
else()
    target_compile_options(VoxelForgeProject PRIVATE -Wall -Wextra -Wpedantic)
endif()
