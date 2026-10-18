#!/bin/bash
# build-xp32.sh
# Cross-compiles llama.cpp for Windows XP 32-bit (Pentium M, SSE2 only)
# Run this from the llama-xp project root (NOT inside llama.cpp)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LLAMA_DIR="$PROJECT_DIR/llama.cpp"
BUILD_DIR="$LLAMA_DIR/build-xp32"
DEPLOY_DIR="$PROJECT_DIR/deploy"
TOOLCHAIN="$PROJECT_DIR/cmake/xp32-toolchain.cmake"

# Set MINGW_ROOT to the MinGW installation directory.
: "${MINGW_ROOT:?Set MINGW_ROOT to the MinGW installation directory}"
MINGW_BIN="$MINGW_ROOT/bin"
if command -v cygpath >/dev/null 2>&1; then
    # Git Bash / MSYS2: PATH needs the /c/... form (a "C:/..." entry is split at the colon),
    # while cmake.exe and the toolchain file need the C:/... form.
    export PATH="$(cygpath -u "$MINGW_BIN"):$PATH"
    MINGW_ROOT="$(cygpath -m "$MINGW_ROOT")"
    MINGW_BIN="$MINGW_ROOT/bin"
else
    export PATH="$MINGW_BIN:$PATH"
fi
export MINGW_ROOT

# Pentium M: SSE2 only (see README) - without these the binary dies with "illegal instruction"
XP_CFLAGS="-march=pentium-m -mno-sse3"
# Strip build-machine paths from __FILE__ (assert messages): otherwise the shipped .exe files contain
# the full source path, including the Windows user name. gcc.exe sees Windows paths, so map those.
if command -v cygpath >/dev/null 2>&1; then
    XP_CFLAGS="$XP_CFLAGS -ffile-prefix-map=$(cygpath -m "$LLAMA_DIR")=llama.cpp -ffile-prefix-map=$(cygpath -m "$PROJECT_DIR")=neuralxp -ffile-prefix-map=$(cygpath -m "$MINGW_ROOT")=mingw"
else
    XP_CFLAGS="$XP_CFLAGS -ffile-prefix-map=$LLAMA_DIR=llama.cpp -ffile-prefix-map=$PROJECT_DIR=neuralxp -ffile-prefix-map=$MINGW_ROOT=mingw"
fi
export XP_CFLAGS
XP_CMAKE_FLAGS=(
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
    -DCMAKE_BUILD_TYPE=Release
    -DGGML_NATIVE=OFF
    -DGGML_AVX=OFF
    -DGGML_AVX2=OFF
    -DGGML_FMA=OFF
    -DGGML_F16C=OFF
    -DGGML_SSE42=OFF
    -DGGML_SSSE3=OFF
    -DGGML_SSE3=OFF
    -DGGML_SSE2=ON
    -DGGML_BMI2=OFF
    -DGGML_CPU_ALL_VARIANTS=OFF
    # No OpenMP: it needs libgomp-1.dll (not shipped) and makes ggml call _putenv_s, which XP's
    # msvcrt.dll does not have. ggml then uses its own threadpool (patched for XP).
    -DGGML_OPENMP=OFF
    -DCMAKE_C_FLAGS="$XP_CFLAGS"
    -DCMAKE_CXX_FLAGS="$XP_CFLAGS"
    # Static: no libgcc_s_dw2-1/libstdc++-6/libwinpthread-1 DLLs needed (the installer never shipped them)
    -DCMAKE_EXE_LINKER_FLAGS="-static -s"
    -DBUILD_SHARED_LIBS=OFF
)

echo "============================================"
echo "  llama.cpp -> Windows XP 32-bit Builder"
echo "============================================"
echo ""

# --- Step 1: Check compiler ---
if ! "$MINGW_BIN/gcc.exe" --version &>/dev/null; then
    echo "ERROR: gcc.exe not found at $MINGW_BIN"
    exit 1
fi

echo "Compiler: $("$MINGW_BIN/gcc.exe" --version | head -1)"
echo ""

# --- Step 2: Clone llama.cpp if needed ---
if [ ! -d "$LLAMA_DIR" ]; then
    echo "[1/5] Cloning llama.cpp..."
    git clone https://github.com/ggml-org/llama.cpp "$LLAMA_DIR"
else
    echo "[1/5] llama.cpp already cloned."
fi

# --- Step 3: Apply XP patches ---
echo "[2/5] Applying XP compatibility patches..."
cd "$LLAMA_DIR"
bash "$SCRIPT_DIR/patch-for-xp.sh"
# NeuralXP's modified examples (system prompt, output format)
cp -r "$PROJECT_DIR/llama-modifications/"* "$LLAMA_DIR/"

# --- Step 4: Configure with CMake ---
echo ""
echo "[3/5] Configuring CMake..."
cmake -B "$BUILD_DIR" \
    "${XP_CMAKE_FLAGS[@]}" \
    -DLLAMA_CURL=OFF \
    -DLLAMA_BUILD_SERVER=OFF \
    -DLLAMA_BUILD_EXAMPLES=ON \
    -DLLAMA_BUILD_TESTS=OFF \
    -DLLAMA_BUILD_TOOLS=OFF \
    -G "MinGW Makefiles"

# --- Step 5: Build ---
# Only what ships: llama-simple / llama-simple-chat link just libllama. ("all" also builds
#  llama.cpp's common library, tools and tests, which use Vista+ APIs and do not compile for XP.)
echo ""
echo "[4/5] Building..."
cmake --build "$BUILD_DIR" --config Release --target llama-simple llama-simple-chat -- -j$(nproc 2>/dev/null || echo 4)

# The GUI runs llama-simple-chat-headless.exe, which the steps above never built
echo "  Building NeuralXP inference helper..."
cmake -S "$PROJECT_DIR/cmake/helper" -B "$BUILD_DIR-helper" \
    -DLLAMA_DIR="$LLAMA_DIR" \
    "${XP_CMAKE_FLAGS[@]}" \
    -G "MinGW Makefiles"
cmake --build "$BUILD_DIR-helper" --config Release --target llama-simple-chat-headless -- -j$(nproc 2>/dev/null || echo 4)

# --- Step 6: Package for deployment ---
echo ""
echo "[5/5] Packaging deployment files..."
mkdir -p "$DEPLOY_DIR"

# Copy binaries
cp "$BUILD_DIR/bin/llama-cli.exe" "$DEPLOY_DIR/" 2>/dev/null || true
cp "$BUILD_DIR/bin/llama-simple.exe" "$DEPLOY_DIR/" 2>/dev/null || true
cp "$BUILD_DIR/bin/llama-simple-chat.exe" "$DEPLOY_DIR/" 2>/dev/null || true
cp "$BUILD_DIR/bin/main.exe" "$DEPLOY_DIR/" 2>/dev/null || true
cp "$BUILD_DIR-helper/llama-simple-chat-headless.exe" "$DEPLOY_DIR/" 2>/dev/null || \
    cp "$BUILD_DIR-helper/bin/llama-simple-chat-headless.exe" "$DEPLOY_DIR/"
# (Everything is linked statically, so no MinGW runtime DLLs need to be copied.)

# Copy the XP launcher batch files (kept in scripts/xp/; deploy/ is not in git)
cp "$SCRIPT_DIR/xp/run-on-xp.bat" "$SCRIPT_DIR/xp/chat-xp.bat" "$DEPLOY_DIR/"

echo ""
echo "============================================"
echo "  BUILD COMPLETE!"
echo "============================================"
echo ""
echo "Deployment files are in: $DEPLOY_DIR/"
echo ""
echo "Next steps:"
echo "  1. Download the model (run scripts/download-model.sh)"
echo "  2. Copy the entire 'deploy' folder to a USB drive"
echo "  3. Plug the USB into your Inspiron 6000"
echo "  4. Run 'run-on-xp.bat' from the USB or copy to C:\\llama"
echo ""

