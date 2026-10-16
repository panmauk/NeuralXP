# Builds the NeuralXP inference helper (llama-simple-chat-headless) against a llama.cpp checkout.
# Shared by the Windows XP build (scripts/build-xp32.sh) and the Linux build (linux/build-linux.sh).
#
#   cmake -S cmake/helper -B build-helper -DLLAMA_DIR=/path/to/llama.cpp [llama/ggml options...]

cmake_minimum_required(VERSION 3.14)
project(neuralxp-helper C CXX)

set(LLAMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../llama.cpp" CACHE PATH "llama.cpp source tree")
if(NOT EXISTS "${LLAMA_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "llama.cpp not found at LLAMA_DIR=${LLAMA_DIR}")
endif()

# Only the libraries are needed; skip llama.cpp's own tools/examples/tests/server.
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(LLAMA_CURL           OFF CACHE BOOL "" FORCE)

add_subdirectory("${LLAMA_DIR}" llama.cpp)

add_executable(llama-simple-chat-headless "${CMAKE_CURRENT_SOURCE_DIR}/../../gui/simple-chat-headless.cpp")
target_link_libraries(llama-simple-chat-headless PRIVATE llama)
target_compile_features(llama-simple-chat-headless PRIVATE cxx_std_17)
install(TARGETS llama-simple-chat-headless RUNTIME DESTINATION bin)
