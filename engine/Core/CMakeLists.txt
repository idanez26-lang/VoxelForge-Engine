add_library(VoxelForgeCore STATIC)

target_sources(
    VoxelForgeCore
    PRIVATE
        src/Application.cpp
        src/FileSystem.cpp
        src/Input/Input.cpp
        src/Layer/Layer.cpp
        src/Layer/LayerStack.cpp
        src/Logger.cpp
        src/Time.cpp
        src/UserDataPaths.cpp
        src/UUID.cpp
        src/Version.cpp
        src/Window/SDLWindow.cpp
        src/Window/Window.cpp
)

target_include_directories(
    VoxelForgeCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    VoxelForgeCore
    PRIVATE
        SDL3::SDL3
        VoxelForgeImGui
        VoxelForgeRenderer
)

target_compile_features(VoxelForgeCore PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(VoxelForgeCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(VoxelForgeCore PRIVATE -Wall -Wextra -Wpedantic)
endif()
