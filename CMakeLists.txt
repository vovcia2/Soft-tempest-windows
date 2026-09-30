cmake_minimum_required(VERSION 3.21)

project(SoftTempest
    VERSION 0.1.0
    DESCRIPTION "Soft TEMPEST display-masking overlay for Windows"
    LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "SoftTempest targets Windows only (MSVC, or MinGW-w64 via cmake/mingw-w64-x86_64.cmake).")
endif()

option(SOFTTEMPEST_RUNTIME_SHADERS
    "Compile HLSL at run time with D3DCompile instead of fxc at build time" OFF)

# ---------------------------------------------------------------------------
# Locate fxc (Windows SDK). Preferred: shaders precompiled to headers.
# ---------------------------------------------------------------------------
set(SOFTTEMPEST_USE_FXC OFF)
if(NOT SOFTTEMPEST_RUNTIME_SHADERS)
    find_program(FXC_EXECUTABLE NAMES fxc fxc.exe)
    if(NOT FXC_EXECUTABLE)
        set(_kit_roots
            "$ENV{ProgramFiles\(x86\)}/Windows Kits/10"
            "$ENV{ProgramFiles}/Windows Kits/10"
            "$ENV{WindowsSdkDir}")
        get_filename_component(_kit_reg
            "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
        if(_kit_reg AND NOT _kit_reg MATCHES "registry")
            list(APPEND _kit_roots "${_kit_reg}")
        endif()
        set(_fxc_candidates)
        foreach(_root IN LISTS _kit_roots)
            if(_root)
                file(TO_CMAKE_PATH "${_root}" _root)
                file(GLOB _found "${_root}/bin/10.*/x64/fxc.exe")
                list(APPEND _fxc_candidates ${_found})
            endif()
        endforeach()
        if(_fxc_candidates)
            list(SORT _fxc_candidates)
            list(REVERSE _fxc_candidates)   # newest SDK first
            list(GET _fxc_candidates 0 FXC_EXECUTABLE)
            set(FXC_EXECUTABLE "${FXC_EXECUTABLE}" CACHE FILEPATH "HLSL compiler" FORCE)
        endif()
    endif()
    if(FXC_EXECUTABLE)
        set(SOFTTEMPEST_USE_FXC ON)
        message(STATUS "HLSL: precompiling with fxc: ${FXC_EXECUTABLE}")
    else()
        message(STATUS "HLSL: fxc not found, falling back to run-time D3DCompile")
    endif()
else()
    message(STATUS "HLSL: run-time D3DCompile requested")
endif()

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(SoftTempest WIN32
    src/main.cpp
    src/OverlayWindow.cpp
    src/OverlayWindow.h
    src/Renderer.cpp
    src/Renderer.h
    src/Settings.cpp
    src/Settings.h
    src/NoiseConfig.h
    src/shaders/noise_vs.hlsl
    src/shaders/noise_ps.hlsl
    res/app.rc
    res/resource.h
)

# HLSL files are listed for IDE visibility only; fxc handles them below.
set_source_files_properties(src/shaders/noise_vs.hlsl src/shaders/noise_ps.hlsl
    PROPERTIES HEADER_FILE_ONLY TRUE VS_TOOL_OVERRIDE "None")

if(MSVC)
    # CMake merges .manifest sources into the linker-generated manifest.
    target_sources(SoftTempest PRIVATE res/app.manifest)
endif()

target_compile_definitions(SoftTempest PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00
)

target_include_directories(SoftTempest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/res
    ${CMAKE_BINARY_DIR}/shaders
)

target_link_libraries(SoftTempest PRIVATE d3d11 dxgi dcomp user32 gdi32 shell32 advapi32)

if(MSVC)
    target_compile_options(SoftTempest PRIVATE /W4 /permissive- /utf-8)
    target_link_options(SoftTempest PRIVATE /SUBSYSTEM:WINDOWS)
else()
    target_compile_options(SoftTempest PRIVATE -Wall -Wextra)
    # Self-contained .exe: no libstdc++/libgcc DLL dependencies.
    target_link_options(SoftTempest PRIVATE -static -mwindows)
endif()

# ---------------------------------------------------------------------------
# Shaders
# ---------------------------------------------------------------------------
set(SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

if(SOFTTEMPEST_USE_FXC)
    function(softtempest_compile_hlsl src profile varname)
        get_filename_component(_name ${src} NAME_WE)
        set(_out ${SHADER_OUT_DIR}/${_name}.h)
        add_custom_command(
            OUTPUT ${_out}
            COMMAND ${FXC_EXECUTABLE} /nologo /T ${profile} /E main /O3
                    /Fh ${_out} /Vn ${varname} ${CMAKE_CURRENT_SOURCE_DIR}/${src}
            MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${src}
            COMMENT "fxc ${profile} ${src}"
            VERBATIM)
        target_sources(SoftTempest PRIVATE ${_out})
        set_source_files_properties(${_out} PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
    endfunction()

    softtempest_compile_hlsl(src/shaders/noise_vs.hlsl vs_5_0 g_noise_vs)
    softtempest_compile_hlsl(src/shaders/noise_ps.hlsl ps_5_0 g_noise_ps)
else()
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/noise_vs.hlsl NOISE_VS_SOURCE)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/noise_ps.hlsl NOISE_PS_SOURCE)
    configure_file(cmake/noise_sources.h.in ${SHADER_OUT_DIR}/noise_sources.h @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        src/shaders/noise_vs.hlsl src/shaders/noise_ps.hlsl cmake/noise_sources.h.in)

    target_compile_definitions(SoftTempest PRIVATE SOFTTEMPEST_RUNTIME_SHADERS)
    target_link_libraries(SoftTempest PRIVATE d3dcompiler)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS SoftTempest RUNTIME DESTINATION .)
