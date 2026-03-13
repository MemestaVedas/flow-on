cmake_minimum_required(VERSION 3.20)
project(flow-on VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
# Compiler flags — Release: maximum speed; Debug: debuggable but still decent
# --------------------------------------------------------------------------
add_compile_options(/arch:AVX2 /W3)
add_compile_options(/FS)  # Fix MSVC C1041 when /MP writes the same PDB

# Release: aggressive optimisation chain
add_compile_options("$<$<CONFIG:Release>:/O2>")      # Maximize speed
add_compile_options("$<$<CONFIG:Release>:/Oi>")      # Enable intrinsic functions
add_compile_options("$<$<CONFIG:Release>:/Ot>")      # Favor fast code
add_compile_options("$<$<CONFIG:Release>:/fp:fast>") # Fast floating point
add_compile_options("$<$<CONFIG:Release>:/GL>")      # Whole program optimization
add_compile_options("$<$<CONFIG:Release>:/Gy>")      # Enable function-level linking
add_compile_options("$<$<CONFIG:Release>:/Gw>")      # Optimize global data
add_link_options("$<$<CONFIG:Release>:/LTCG>")      # Link-time code generation
add_link_options("$<$<CONFIG:Release>:/OPT:REF>")    # Eliminate unreferenced functions
add_link_options("$<$<CONFIG:Release>:/OPT:ICF>")    # Identical COMDAT folding

# Debug: keep /Od but still allow vectorisation
add_compile_options("$<$<CONFIG:Debug>:/fp:fast>")

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

# --------------------------------------------------------------------------
# whisper.cpp — built as a static library
# --------------------------------------------------------------------------
set(WHISPER_AVX2           ON  CACHE BOOL "" FORCE)
set(WHISPER_F16C           ON  CACHE BOOL "" FORCE)
set(WHISPER_AVX            ON  CACHE BOOL "" FORCE)  # Enable AVX
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS   OFF CACHE BOOL "" FORCE)  # Disable warnings for faster build

# Enable OpenMP so whisper.cpp matrix ops run on ALL cores
set(GGML_OPENMP            ON  CACHE BOOL "" FORCE)

# Native optimisation: auto-detect AVX2 / FMA / SSE4.2
set(GGML_NATIVE            ON  CACHE BOOL "" FORCE)
set(GGML_FMA               ON  CACHE BOOL "" FORCE)
set(GGML_AVX               ON  CACHE BOOL "" FORCE)
set(GGML_AVX2              ON  CACHE BOOL "" FORCE)
set(GGML_F16C              ON  CACHE BOOL "" FORCE)

# Flash attention — reduces memory traffic, faster on long sequences
set(WHISPER_FLASH_ATTN     ON  CACHE BOOL "" FORCE)

# Uncomment next two lines for NVIDIA CUDA acceleration (5-10x faster):
# set(WHISPER_CUBLAS ON CACHE BOOL "" FORCE)
# set(GGML_CUDA      ON CACHE BOOL "" FORCE)
add_subdirectory(external/whisper.cpp)

# --------------------------------------------------------------------------
# Main executable — WIN32 subsystem: no console window
# --------------------------------------------------------------------------
add_executable(flow-on WIN32
    src/main.cpp
    src/audio_manager.cpp
    src/transcriber.cpp
    src/formatter.cpp
    src/injector.cpp
    src/overlay.cpp
    src/snippet_engine.cpp
    src/config_manager.cpp
    # dashboard.cpp compiles in Win32 fallback mode by default.
    # For full WinUI 3: install Microsoft.WindowsAppSDK via NuGet in the VS
    # project, add ENABLE_WINUI3_DASHBOARD to the preprocessor definitions,
    # and rebuild.
    src/dashboard.cpp
    flow-on.rc      # application icon + version info
)

target_include_directories(flow-on PRIVATE
    src/
    external/
    external/whisper.cpp/include/
    external/whisper.cpp/ggml/include/
    ${CMAKE_SOURCE_DIR}          # for Resource.h referenced as "../Resource.h"
)

# --------------------------------------------------------------------------
# System libraries
# --------------------------------------------------------------------------
target_link_libraries(flow-on PRIVATE
    whisper
    # Audio + core Win32
    winmm
    # Shell / tray
    user32 shell32 gdi32 gdiplus
    # Direct2D overlay (Phase 7)
    d2d1 dwrite
    # Misc
    ole32 oleaut32 uuid
)

# --------------------------------------------------------------------------
# Post-build: mirror model into output folder so the binary finds it
# --------------------------------------------------------------------------
add_custom_command(TARGET flow-on POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:flow-on>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/models/ggml-base.en.bin"
        "$<TARGET_FILE_DIR:flow-on>/models/ggml-base.en.bin"
    COMMENT "Copying Whisper model to output directory"
)

# --------------------------------------------------------------------------
# Post-build: copy whisper.cpp DLL files to executable directory
# --------------------------------------------------------------------------
add_custom_command(TARGET flow-on POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:whisper>"
        "$<TARGET_FILE_DIR:flow-on>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ggml>"
        "$<TARGET_FILE_DIR:flow-on>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ggml-base>"
        "$<TARGET_FILE_DIR:flow-on>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ggml-cpu>"
        "$<TARGET_FILE_DIR:flow-on>"
    COMMENT "Copying Whisper DLL dependencies to executable directory"
)

# --------------------------------------------------------------------------
# IDE source grouping for Visual Studio Solution Explorer
# --------------------------------------------------------------------------
source_group("Source Files"  REGULAR_EXPRESSION "src/.*\\.cpp")
source_group("Header Files"  REGULAR_EXPRESSION "src/.*\\.h")
source_group("External"      REGULAR_EXPRESSION "external/.*")
