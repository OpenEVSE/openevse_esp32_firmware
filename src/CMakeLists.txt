# Used only by envs with `framework = arduino, espidf` (the hybrid build).
# Envs on the plain `framework = arduino` (prebuilt) path never invoke CMake
# and ignore this file entirely; PlatformIO's SCons build still compiles
# src/*.cpp itself in both modes, this file only registers the glob for
# ESP-IDF's component graph.
#
# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources})
