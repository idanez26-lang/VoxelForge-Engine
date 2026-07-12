add_library(VoxelForgeCore STATIC)

target_sources(
    VoxelForgeCore
    PRIVATE
        src/Application.cpp
        src/Logger.cpp
        src/Version.cpp
)

target_include_directories(
    VoxelForgeCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(VoxelForgeCore PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(VoxelForgeCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(VoxelForgeCore PRIVATE -Wall -Wextra -Wpedantic)
endif()
