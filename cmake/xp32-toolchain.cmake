# CMake Toolchain File for Windows XP 32-bit (Pentium M / SSE2 only)
# Used to cross-compile llama.cpp from a modern Windows PC

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)

# MinGW-w64 i686 compilers - exact path on this machine
# Override with the MINGW_ROOT environment variable (build-xp32.sh exports it)
if(DEFINED ENV{MINGW_ROOT})
    set(MINGW_ROOT "$ENV{MINGW_ROOT}")
else()
    set(MINGW_ROOT "$MINGW_ROOT")
endif()
set(CMAKE_C_COMPILER "${MINGW_ROOT}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${MINGW_ROOT}/bin/g++.exe")
set(CMAKE_RC_COMPILER "${MINGW_ROOT}/bin/windres.exe")

set(CMAKE_FIND_ROOT_PATH "${MINGW_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Target Windows XP 32-bit (0x0501)
add_compile_definitions(_WIN32_WINNT=0x0501)
add_compile_definitions(WINVER=0x0501)

set(GGML_WIN_VER "0x0501" CACHE STRING "ggml: Windows version" FORCE)
