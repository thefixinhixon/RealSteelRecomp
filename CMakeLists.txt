# realsteel - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(realsteel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Linux needs SSSE3/SSE4.1 for the SDK's memory.cpp (same as condemned2)
if(NOT WIN32)
    add_compile_options(-mssse3 -msse4.1)
endif()

include(generated/rexglue.cmake)

# Sources
set(REALSTEEL_SOURCES
    src/main.cpp
    src/stubs.cpp
)

if(WIN32)
    add_executable(realsteel WIN32 ${REALSTEEL_SOURCES})
else()
    add_executable(realsteel ${REALSTEEL_SOURCES})
endif()

rexglue_setup_target(realsteel)

# imgui headers ship inside the SDK tree but aren't added to the include
# path by rexglue_setup_target; add them explicitly (same as condemned2).
if(REXSDK_DIR)
    target_include_directories(realsteel PRIVATE
        "${REXSDK_DIR}/thirdparty/imgui"
    )
endif()

if(TARGET imgui::imgui)
    target_link_libraries(realsteel PRIVATE imgui::imgui)
elseif(TARGET imgui)
    target_link_libraries(realsteel PRIVATE imgui)
endif()

# Title update build: realsteel-tu, recompiled from default.xex with title
# update 2's default.xexp applied (generated-tu/, made by scripts/codegen-tu.sh).
# Same app and runtime; it needs the title update installed at run time (see
# src/title_update.h). The launcher picks it when a matching update is present.
set(_rs_tu_dir "${CMAKE_CURRENT_SOURCE_DIR}/generated-tu/default")
if(EXISTS "${_rs_tu_dir}/sources.cmake")
    include("${_rs_tu_dir}/sources.cmake")
    set(REALSTEEL_TU_GENERATED_SOURCES ${GENERATED_SOURCES})
    set_source_files_properties(${REALSTEEL_TU_GENERATED_SOURCES}
        PROPERTIES COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS}")

    add_library(realsteel_tu_recomp OBJECT ${REALSTEEL_TU_GENERATED_SOURCES})
    target_include_directories(realsteel_tu_recomp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${_rs_tu_dir})
    target_link_libraries(realsteel_tu_recomp PRIVATE rex::runtime)
    rexglue_apply_target_settings(realsteel_tu_recomp)
    rexglue_apply_recomp_settings(realsteel_tu_recomp "${_rs_tu_dir}/realsteel_pch.h")

    set(REALSTEEL_TU_SOURCES src/main_tu.cpp src/stubs.cpp)
    if(WIN32)
        add_executable(realsteel-tu WIN32 ${REALSTEEL_TU_SOURCES})
    else()
        add_executable(realsteel-tu ${REALSTEEL_TU_SOURCES})
    endif()
    target_compile_definitions(realsteel-tu PRIVATE REALSTEEL_TITLE_UPDATE=1)
    target_include_directories(realsteel-tu PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${_rs_tu_dir})
    target_link_libraries(realsteel-tu PRIVATE realsteel_tu_recomp rex::runtime)
    if(REXSDK_DIR)
        target_include_directories(realsteel-tu PRIVATE "${REXSDK_DIR}/thirdparty/imgui")
    endif()
    if(TARGET imgui::imgui)
        target_link_libraries(realsteel-tu PRIVATE imgui::imgui)
    elseif(TARGET imgui)
        target_link_libraries(realsteel-tu PRIVATE imgui)
    endif()
    rexglue_configure_target(realsteel-tu)
endif()

# Keep the runtime libraries next to the executable after every build, so the
# build folder is a runnable, matched set (the exe uses RUNPATH=$ORIGIN, and
# mixing an exe with runtime libraries from a different build corrupts the
# heap). A separate always-run target: a POST_BUILD step on the exe would miss
# rebuilds that only touch a library (e.g. the GPU plugin).
set(_rs_runtime_copy_commands "")
foreach(_rs_runtime_lib rexruntime rexgpu-xenos)
    if(TARGET ${_rs_runtime_lib})
        list(APPEND _rs_runtime_copy_commands
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${_rs_runtime_lib}> $<TARGET_FILE_DIR:realsteel>)
        list(APPEND _rs_runtime_copy_deps ${_rs_runtime_lib})
    endif()
endforeach()
if(_rs_runtime_copy_commands)
    add_custom_target(realsteel_runtime_libs ALL ${_rs_runtime_copy_commands} VERBATIM)
    add_dependencies(realsteel_runtime_libs realsteel ${_rs_runtime_copy_deps})
    if(TARGET realsteel-tu)
        add_dependencies(realsteel_runtime_libs realsteel-tu)
    endif()
endif()
