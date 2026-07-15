cmake_minimum_required(VERSION 3.24)

project(
    VoxelForgeEngine
    VERSION 0.1.2
    DESCRIPTION "Universal AI-assisted voxel creation platform"
    LANGUAGES CXX
)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VF_BUILD_EDITOR "Build the VoxelForge editor executable" ON)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.10
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(
    DearImGui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.8-docking
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(DearImGui)

function(vf_fix_msvc_ninja_showincludes_prefix)
    if(NOT MSVC OR MSVC_VERSION LESS 1951 OR NOT CMAKE_GENERATOR MATCHES "^Ninja")
        return()
    endif()

    # MSVC 19.51 emits /showIncludes output as UTF-8. CMake 4.3.1 may
    # decode that output with the active Windows console code page instead,
    # which prevents Ninja from recording header dependencies.
    set(show_includes_dir "${CMAKE_BINARY_DIR}/CMakeFiles/VoxelForgeShowIncludes")
    file(MAKE_DIRECTORY "${show_includes_dir}")
    file(WRITE "${show_includes_dir}/header.h" "\n")
    file(WRITE "${show_includes_dir}/source.cpp" "#include \"header.h\"\n")

    execute_process(
        COMMAND
            "${CMAKE_CXX_COMPILER}"
            ${CMAKE_CXX_COMPILER_ARG1}
            /nologo
            /showIncludes
            /Zs
            source.cpp
        WORKING_DIRECTORY "${show_includes_dir}"
        OUTPUT_VARIABLE show_includes_output
        ERROR_VARIABLE show_includes_error
        RESULT_VARIABLE show_includes_result
        ENCODING UTF-8
    )

    if(
        show_includes_result EQUAL 0
        AND show_includes_output MATCHES
            "(^|\n)([^:\n][^:\n]+:[^:\n]*[^: \n][^: \n]:?[ \t]+)([A-Za-z]:\\\\|\\./|\\.\\\\|/)"
    )
        set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}" PARENT_SCOPE)
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}" PARENT_SCOPE)
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}" PARENT_SCOPE)
    else()
        message(
            WARNING
            "Unable to detect the UTF-8 MSVC /showIncludes prefix: ${show_includes_error}"
        )
    endif()
endfunction()

vf_fix_msvc_ninja_showincludes_prefix()

add_library(VoxelForgeImGui STATIC)

target_sources(
    VoxelForgeImGui
    PRIVATE
        ${dearimgui_SOURCE_DIR}/imgui.cpp
        ${dearimgui_SOURCE_DIR}/imgui_demo.cpp
        ${dearimgui_SOURCE_DIR}/imgui_draw.cpp
        ${dearimgui_SOURCE_DIR}/imgui_tables.cpp
        ${dearimgui_SOURCE_DIR}/imgui_widgets.cpp
        ${dearimgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${dearimgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)

target_include_directories(
    VoxelForgeImGui
    PUBLIC
        ${dearimgui_SOURCE_DIR}
        ${dearimgui_SOURCE_DIR}/backends
)

target_link_libraries(
    VoxelForgeImGui
    PUBLIC
        SDL3::SDL3
)

target_compile_features(VoxelForgeImGui PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(VoxelForgeImGui PRIVATE /W3)
else()
    target_compile_options(VoxelForgeImGui PRIVATE -Wall -Wextra)
endif()

# Renderer is lower-level than Core from this sprint onward.
add_subdirectory(engine/Renderer)
add_subdirectory(engine/Core)
add_subdirectory(engine/Asset)
add_subdirectory(engine/Project)
add_subdirectory(engine/Scene)

if(VF_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
