cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(VASDL)

# Find SDL3
find_package(SDL3 REQUIRED)

# Fetch Dear ImGui
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# Fetch CLI11 for command-line parsing
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.6.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cli11)

# Build ImGui as a static library with SDL3 backends
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# Suppress warnings in ImGui source
if(NOT MSVC)
    target_compile_options(imgui PRIVATE -w)
endif()

# Add the SDL frontend executable
add_executable(VASDL
    main.cpp
    App.cpp
    SDLAudio.cpp
)

# Require C++20 (for designated initializers, etc.)
target_compile_features(VASDL PRIVATE cxx_std_20)

# Warning flags
if(NOT MSVC)
    target_compile_options(VASDL PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Link against VACore, SDL3, and ImGui
target_link_libraries(VASDL PRIVATE VACore SDL3::SDL3 imgui CLI11::CLI11)
