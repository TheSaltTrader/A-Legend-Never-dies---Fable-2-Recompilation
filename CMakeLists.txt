# fable2 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(fable2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(FABLE2_SOURCES
    src/main.cpp
    src/fable2_dlc.cpp
    src/fable2_saveimport.cpp
    src/fable2_autoskip.cpp
    src/fable2_hwdetect.cpp
    src/fable2_perf.cpp
    src/fable2_textool.cpp
    src/fable2_disc.cpp
    src/fable2_menu.cpp
    src/fable2_platform.cpp
    src/fable2_texnotify.cpp
    src/fable2_diagnostics.cpp
    src/fable2_stage.cpp
    src/patch_hooks.cpp
)

if(WIN32)
    add_executable(fable2 WIN32 ${FABLE2_SOURCES})
else()
    add_executable(fable2 ${FABLE2_SOURCES})
endif()

# The port's own version, from the VERSION file, baked in so the window title
# and the diagnostics bundle name the build they came from. The SDK's own
# title string names the SDK version, which never changes between our builds.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FABLE2_VERSION_TEXT)
string(STRIP "${FABLE2_VERSION_TEXT}" FABLE2_VERSION_TEXT)
target_compile_definitions(fable2 PRIVATE FABLE2_VERSION="${FABLE2_VERSION_TEXT}")

# GPU_PLUGINS stages the SDK's Xenos emulation plugin (rexgpu-xenos.dll) next
# to the executable. It is runtime-loaded, never linked.
rexglue_setup_target(fable2 GPU_PLUGINS xenos)

# DbgHelp resolves recompiled sub_<guest address> names for the
# FP trap diagnostic (src/fable2_fptrap.h).
if (WIN32)
    target_link_libraries(fable2 PRIVATE dbghelp)
endif()

