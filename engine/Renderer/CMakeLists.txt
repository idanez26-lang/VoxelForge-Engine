add_library(VoxelForgeRenderer STATIC)

target_sources(
    VoxelForgeRenderer
    PRIVATE
        src/Renderer.cpp
)

target_include_directories(
    VoxelForgeRenderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    VoxelForgeRenderer
    PUBLIC
        SDL3::SDL3
)

target_compile_features(VoxelForgeRenderer PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(VoxelForgeRenderer PRIVATE /W4 /permissive-)
else()
    target_compile_options(VoxelForgeRenderer PRIVATE -Wall -Wextra -Wpedantic)
endif()
