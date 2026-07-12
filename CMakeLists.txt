cmake_minimum_required(VERSION 3.24)

project(
    VoxelForgeEngine
    VERSION 0.0.2
    DESCRIPTION "Universal AI-assisted voxel creation platform"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VF_BUILD_EDITOR "Build the VoxelForge editor executable" ON)

add_subdirectory(engine/Core)

if(VF_BUILD_EDITOR)
    add_subdirectory(editor)
endif()
