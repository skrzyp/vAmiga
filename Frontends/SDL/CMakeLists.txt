cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(VASDL)

# Find SDL3
find_package(SDL3 REQUIRED)

# Add the SDL frontend executable
add_executable(VASDL
    main.cpp
    SDLRenderer.cpp
    SDLAudio.cpp
)

# Link against VACore and SDL3
# VACore exports its include directories as PUBLIC, so no extra includes needed
target_link_libraries(VASDL PRIVATE VACore SDL3::SDL3)
