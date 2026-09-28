# fable2 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(fable2 LANGUAGES CXX RC)

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
    src/fable2_update.cpp
    src/fable2_disc.cpp
    src/fable2_menu.cpp
    src/fable2_platform.cpp
    src/fable2_texnotify.cpp
    src/fable2_keyremap.cpp
    src/fable2_diagnostics.cpp
    src/fable2_stage.cpp
    src/fable2_gdb.cpp
    src/fable2_crashdump.cpp
    src/fable2_profiler.cpp
    src/fable2_titleupdate.cpp
    src/patch_hooks.cpp
    src/native_gpu_trace.cpp
    src/fable2_p2_census.cpp
    src/fable2_native_gs.cpp
    src/fable2_gpu_cvars.cpp
    src/native_gpu_dump.cpp
    src/native_gpu_present.cpp
    src/native_gpu_census.cpp
    src/native_gpu_shader_census.cpp
    src/native_gpu_sdk_xlat.cpp
    src/native_gpu_sdk_consts.cpp
    src/native_gpu_rtc.cpp
    src/native_gpu_backend.cpp
    src/native_gpu_sdk_tess.cpp
    src/native_gpu_state_decode.cpp
    # The SDK's Xenos shader translator, compiled INTO the app (branch native-gpu).
    # rexruntime.lib exports none of it (0 ShaderTranslator symbols; it lives in
    # rexgpu-xenos.dll with hidden visibility), so the BSD sources are vendored
    # verbatim from rexglue-src (see src/native_gpu_xlat/ORIGIN.txt). The three
    # cvars they define (dxbc_switch, dxbc_source_map,
    # draw_resolution_scaled_texture_offsets) are also registered by the plugin;
    # rex::cvar rejects the duplicate registration and keeps the first owner,
    # so both copies read the same value and nothing double-registers.
    src/native_gpu_xlat/ucode.cpp
    src/native_gpu_xlat/shader.cpp
    src/native_gpu_xlat/translator.cpp
    src/native_gpu_xlat/translator_disasm.cpp
    src/native_gpu_xlat/dxbc.cpp
    src/native_gpu_xlat/dxbc_translator.cpp
    src/native_gpu_xlat/dxbc_translator_alu.cpp
    src/native_gpu_xlat/dxbc_translator_fetch.cpp
    src/native_gpu_xlat/dxbc_translator_memexport.cpp
    src/native_gpu_xlat/dxbc_translator_om.cpp
    src/native_gpu_xlat/thirdparty/dxbc/DXBCChecksum.cpp
    src/native_gpu_xlat/xlat_support.cpp
    src/native_gpu_xlat/register_file.cpp
    src/native_gpu_xlat/registers.cpp
    src/native_gpu_xlat/draw_util_vendored.cpp
    src/native_gpu_xlat/pipeline_gs_vendored.cpp
    src/native_gpu_xlat/rt_cache_vendored.cpp
    src/native_gpu_xlat/draw_extent_estimator_vendored.cpp
    src/native_gpu_xlat/shader_interpreter_vendored.cpp
    src/native_gpu_xlat/rtc_d3d12/render_target_cache.cpp
    src/native_gpu_xlat/rtc_d3d12/deferred_command_list.cpp
    src/native_gpu_xlat/rtc_d3d12/d3d12_cpu_descriptor_pool.cpp
    src/native_gpu_xlat/rtc_d3d12/d3d12_upload_buffer_pool.cpp
    src/native_gpu_xlat/rtc_d3d12/d3d12_util.cpp
    src/native_gpu_xlat/rtc_d3d12/facade.cpp
    src/native_gpu_xlat/rtc_d3d12/shared_memory_base.cpp
    src/native_gpu_xlat/rtc_d3d12/shared_memory.cpp
    src/native_gpu_xlat/rtc_d3d12/texture_cache_base.cpp
    src/native_gpu_xlat/rtc_d3d12/texture_conversion.cpp
    src/native_gpu_xlat/rtc_d3d12/texture_extent.cpp
    src/native_gpu_xlat/rtc_d3d12/texture_info.cpp
    src/native_gpu_xlat/rtc_d3d12/texture_info_formats.cpp
    src/native_gpu_xlat/rtc_d3d12/texture_util.cpp
    src/native_gpu_xlat/rtc_d3d12/texture_cache.cpp
    src/native_gpu_xlat/rtc_d3d12/command_processor.cpp
    src/native_gpu_xlat/rtc_d3d12/pipeline_cache.cpp
    src/native_gpu_xlat/rtc_d3d12/primitive_processor.cpp
    src/native_gpu_xlat/rtc_d3d12/primitive_processor_base.cpp
    src/native_gpu_xlat/rtc_d3d12/shader.cpp
    src/native_gpu_xlat/rtc_d3d12/d3d12_descriptor_heap_pool.cpp
    src/native_gpu_xlat/rtc_d3d12/cp_base.cpp
    src/native_gpu_xlat/rtc_d3d12/sampler_info.cpp
    src/native_gpu_xlat/rtc_d3d12/flags.cpp
)

# A build made with the disc's title update beside the executable the codegen
# read (assets/default.xexp) is a different game version, and the setup screen
# says so. The runtime applies the same patch at launch from game/default.xexp.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/default.xexp")
    set(FABLE2_COMPILED_WITH_PATCH ON)
else()
    set(FABLE2_COMPILED_WITH_PATCH OFF)
endif()

if(WIN32)
    # The icon resource (resources/fable2.rc -> fable2.ico) rides in the
    # executable, so Explorer, the desktop and the taskbar show it.
    add_executable(fable2 WIN32 ${FABLE2_SOURCES} resources/fable2.rc)
else()
    add_executable(fable2 ${FABLE2_SOURCES})
endif()
# The vendored SDK shader translator includes "thirdparty/dxbc/DXBCChecksum.h" relative to this dir.
target_include_directories(fable2 PRIVATE src/native_gpu_xlat)
# The translator is vendored from the commit the installed rexgpu-xenos.dll was
# built from (so the in-app translation and the plugin's are one codegen, and the
# DXBC differential compares like with like). That commit's translator needs
# three headers newer than the installed SDK's - additive changes only - which
# are vendored under include/ and must win over the SDK's copies.
target_include_directories(fable2 BEFORE PRIVATE src/native_gpu_xlat/include)

# The port's own version, from the VERSION file, baked in so the window title
# and the diagnostics bundle name the build they came from. The SDK's own
# title string names the SDK version, which never changes between our builds.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FABLE2_VERSION_TEXT)
string(STRIP "${FABLE2_VERSION_TEXT}" FABLE2_VERSION_TEXT)
# Re-run CMake when VERSION changes, so a bump is picked up even by an
# incremental `cmake --build` that would otherwise reuse the old configure.
# Without this, `--build` after a version bump silently kept the OLD
# FABLE2_VERSION: v0.2.14 shipped an exe that reported v0.2.13 and looped the
# updater (it always saw the new release as "available"). 2026-09-22.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
target_compile_definitions(fable2 PRIVATE FABLE2_VERSION="${FABLE2_VERSION_TEXT}")
if(FABLE2_COMPILED_WITH_PATCH)
    target_compile_definitions(fable2 PRIVATE FABLE2_COMPILED_WITH_PATCH=1)
endif()

# No GPU plugin since 1.2.0 (2026-09-28): the game's own graphics system (src/fable2_native_gs.cpp) is the only
# renderer and the exe registers every GPU setting itself (src/fable2_gpu_cvars.cpp), so rexgpu-xenos.dll is neither
# staged nor loaded.
rexglue_setup_target(fable2)

# Line tables for the app's OWN sources and a PDB to hold them. Release builds
# the ~500 recompiled units with -g0 (see tools/build.cmd) and that flag rides
# on the target, so it reached these files too: the sampling profiler
# (FABLE2_PROFILE) could name a generated function through the export table
# but every native-GPU-path function read as a bare RVA (leg P5, 2026-09-23:
# "fable2+482b93d 58.5%"). A source property is appended after the target's
# options, so the later -g wins; the recompiled units keep their -g0.
set_source_files_properties(${FABLE2_SOURCES} PROPERTIES COMPILE_OPTIONS "-gline-tables-only")
target_link_options(fable2 PRIVATE "LINKER:/DEBUG")

# SDL's community controller database, read beside the executable at startup
# (fable2_app.h hands it to the runtime when present). Without it the runtime
# logged "gamecontrollerdb.txt does not exist" at every launch and fell back
# to SDL's built-in mappings, which cover far fewer pads (2026-09-12).
add_custom_command(TARGET fable2 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/gamecontrollerdb.txt"
          "$<TARGET_FILE_DIR:fable2>/gamecontrollerdb.txt")

# Native GPU backend (branch native-gpu): Plume (MIT render HAL, D3D12) linked
# into the executable for the shadow window / native draws. Point
# NGPU_PLUME_DIR at a checkout of Plume and NGPU_PLUME_LIB at its built
# static library; both default to the spike workspace. Off when the library
# is missing, so a stock checkout still builds.
set(NGPU_PLUME_DIR "C:/Users/renoi/ClaudeCode/NativeGPU/reference/plume" CACHE PATH "Plume source dir (headers)")
set(NGPU_PLUME_LIB "C:/Users/renoi/ClaudeCode/NativeGPU/build/plume/plume.lib" CACHE FILEPATH "Plume static library")
if(WIN32 AND EXISTS "${NGPU_PLUME_LIB}" AND EXISTS "${NGPU_PLUME_DIR}/plume_render_interface.h")
    message(STATUS "fable2: native GPU backend ON (Plume at ${NGPU_PLUME_LIB})")
    target_compile_definitions(fable2 PRIVATE FABLE2_NATIVE_GPU=1)
    target_include_directories(fable2 PRIVATE "${NGPU_PLUME_DIR}" "${NGPU_PLUME_DIR}/contrib/D3D12MemoryAllocator/include")
    target_link_libraries(fable2 PRIVATE "${NGPU_PLUME_LIB}" d3d12 dxgi dxguid user32 gdi32)
else()
    message(STATUS "fable2: native GPU backend OFF (no Plume library)")
endif()

# ngpu_backend.dll (2026-09-26): the transplanted backend + lockstep glue as a GAME-AGNOSTIC module (C API in
# src/ngpu_backend_dll/ngpu_backend_api.h) for other ReXGlue titles (Ninja Gaiden II first). Built from the SAME
# backend sources as the executable - the list is filtered out of FABLE2_SOURCES so the two cannot drift.
option(NGPU_BUILD_BACKEND_DLL "Build ngpu_backend.dll (the game-agnostic native backend)" ON)
if(WIN32 AND NGPU_BUILD_BACKEND_DLL)
    set(NGPU_BACKEND_DLL_SOURCES ${FABLE2_SOURCES})
    list(FILTER NGPU_BACKEND_DLL_SOURCES INCLUDE REGEX "src/native_gpu_xlat/|src/native_gpu_backend\\.cpp")
    add_library(ngpu_backend SHARED src/ngpu_backend_dll/ngpu_backend_dll.cpp ${NGPU_BACKEND_DLL_SOURCES})
    target_include_directories(ngpu_backend PRIVATE src src/native_gpu_xlat)
    target_include_directories(ngpu_backend BEFORE PRIVATE src/native_gpu_xlat/include)
    target_link_libraries(ngpu_backend PRIVATE rex::runtime d3d12 dxgi dxguid user32)
    if(COMMAND rexglue_apply_target_settings)
        rexglue_apply_target_settings(ngpu_backend)
    endif()
    set_source_files_properties(src/ngpu_backend_dll/ngpu_backend_dll.cpp PROPERTIES COMPILE_OPTIONS "-gline-tables-only")
    target_link_options(ngpu_backend PRIVATE "LINKER:/DEBUG")
    set_target_properties(ngpu_backend PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:fable2>")
endif()

# DbgHelp resolves recompiled sub_<guest address> names for the
# FP trap diagnostic (src/fable2_fptrap.h).
if (WIN32)
    # WinHTTP: the update check and download (src/fable2_update.cpp).
    target_link_libraries(fable2 PRIVATE dbghelp winhttp)
endif()

