cmake_minimum_required(VERSION 3.20)
project(flow-on VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
# Compiler flags
# /O2 /fp:fast are incompatible with /RTC1 (Debug runtime checks), so they
# are scoped to Release only.  /arch:AVX2 is always on — Whisper requires it.
# --------------------------------------------------------------------------
add_compile_options(/arch:AVX2 /W3)
add_compile_options("$<$<CONFIG:Release>:/O2>")
add_compile_options("$<$<CONFIG:Release>:/fp:fast>")
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
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Uncomment next two lines for NVIDIA GPU acceleration (requires CUDA Toolkit):
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
        "${CMAKE_SOURCE_DIR}/models/ggml-tiny.en.bin"
        "$<TARGET_FILE_DIR:flow-on>/models/ggml-tiny.en.bin"
    COMMENT "Copying Whisper model to output directory"
)

# --------------------------------------------------------------------------
# IDE source grouping for Visual Studio Solution Explorer
# --------------------------------------------------------------------------
source_group("Source Files"  REGULAR_EXPRESSION "src/.*\\.cpp")
source_group("Header Files"  REGULAR_EXPRESSION "src/.*\\.h")
source_group("External"      REGULAR_EXPRESSION "external/.*")
