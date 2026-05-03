#!/bin/bash
# patch-for-xp.sh
# Patches llama.cpp source tree for Windows XP 32-bit compatibility
# Run this from inside the cloned llama.cpp directory

set -e

echo "=== Patching llama.cpp for Windows XP 32-bit ==="
echo ""

# Check we're in the right directory
if [ ! -f "CMakeLists.txt" ] || [ ! -d "ggml" ]; then
    echo "ERROR: Run this script from the root of the llama.cpp repository!"
    exit 1
fi

# --- 1. Downgrade cpp-httplib to v0.15.3 (newer versions block pre-Win8.1) ---
echo "[1/4] Downgrading cpp-httplib to v0.15.3..."
if [ -f "vendor/cpp-httplib/httplib.h" ]; then
    cp vendor/cpp-httplib/httplib.h vendor/cpp-httplib/httplib.h.backup
    curl -sL https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h \
        -o vendor/cpp-httplib/httplib.h
    echo "  Done. Original backed up to httplib.h.backup"
elif [ -f "examples/server/httplib.h" ]; then
    cp examples/server/httplib.h examples/server/httplib.h.backup
    curl -sL https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h \
        -o examples/server/httplib.h
    echo "  Done. Original backed up to httplib.h.backup"
else
    echo "  WARNING: Could not find httplib.h - skipping (server may not build)"
fi

# --- 2. Replace cpp-httplib CMakeLists.txt ---
echo "[2/4] Replacing cpp-httplib CMakeLists.txt..."
if [ -d "vendor/cpp-httplib" ]; then
    cat > vendor/cpp-httplib/CMakeLists.txt << 'CMAKEOF'
set(TARGET cpp-httplib)
find_package(Threads REQUIRED)
add_library(${TARGET} INTERFACE)
target_include_directories(${TARGET} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TARGET} INTERFACE Threads::Threads)
if (WIN32)
    target_link_libraries(${TARGET} INTERFACE ws2_32)
endif()
target_compile_features(${TARGET} INTERFACE cxx_std_17)
target_compile_definitions(${TARGET} INTERFACE
    CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH=1048576
    CPPHTTPLIB_LISTEN_BACKLOG=512
    CPPHTTPLIB_REQUEST_URI_MAX_LENGTH=32768
    CPPHTTPLIB_TCP_NODELAY=1
)
CMAKEOF
    echo "  Done."
else
    echo "  WARNING: vendor/cpp-httplib not found - skipping"
fi

# --- 3. Fix Vista+ threading APIs -> XP-compatible ---
echo "[3/4] Patching threading primitives for XP compatibility..."

# Fix SRWLOCK -> CRITICAL_SECTION in ggml source files
GGML_FILES=$(find . -name "*.c" -o -name "*.cpp" -o -name "*.h" | head -500)
PATCHED=0

for f in $GGML_FILES; do
    if grep -q "SRWLOCK\|InitializeSRWLock\|AcquireSRWLock\|ReleaseSRWLock" "$f" 2>/dev/null; then
        echo "  Patching $f (SRWLOCK -> CRITICAL_SECTION)..."
        sed -i 's/SRWLOCK/CRITICAL_SECTION/g' "$f"
        sed -i 's/InitializeSRWLock\(Exclusive\)\?/InitializeCriticalSection/g' "$f"
        sed -i 's/AcquireSRWLockExclusive/EnterCriticalSection/g' "$f"
        sed -i 's/ReleaseSRWLockExclusive/LeaveCriticalSection/g' "$f"
        sed -i 's/AcquireSRWLockShared/EnterCriticalSection/g' "$f"
        sed -i 's/ReleaseSRWLockShared/LeaveCriticalSection/g' "$f"
        PATCHED=$((PATCHED + 1))
    fi
done

# Fix CONDITION_VARIABLE -> manual Event-based sync
for f in $GGML_FILES; do
    if grep -q "CONDITION_VARIABLE\|InitializeConditionVariable\|SleepConditionVariable\|WakeConditionVariable\|WakeAllConditionVariable" "$f" 2>/dev/null; then
        echo "  Patching $f (CONDITION_VARIABLE -> Event)..."
        sed -i 's/CONDITION_VARIABLE/HANDLE/g' "$f"
        sed -i 's/InitializeConditionVariable(\([^)]*\))/(*(\1) = CreateEvent(NULL, TRUE, FALSE, NULL))/g' "$f"
        sed -i 's/SleepConditionVariableCS(\([^,]*\),\([^,]*\),[^)]*)/WaitForSingleObject(*(\1), INFINITE); ResetEvent(*(\1))/g' "$f"
        sed -i 's/WakeConditionVariable(\([^)]*\))/SetEvent(*(\1))/g' "$f"
        sed -i 's/WakeAllConditionVariable(\([^)]*\))/SetEvent(*(\1))/g' "$f"
        PATCHED=$((PATCHED + 1))
    fi
done

if [ $PATCHED -eq 0 ]; then
    echo "  No Vista+ APIs found - source may already be compatible or API names changed."
    echo "  Check manually if build fails with unresolved symbols."
else
    echo "  Patched $PATCHED file(s)."
fi

# --- 4. Add _WIN32_WINNT define to ggml headers if not present ---
echo "[4/4] Ensuring _WIN32_WINNT=0x0501 is defined..."
MAIN_HEADER=""
if [ -f "ggml/include/ggml.h" ]; then
    MAIN_HEADER="ggml/include/ggml.h"
elif [ -f "ggml.h" ]; then
    MAIN_HEADER="ggml.h"
fi

if [ -n "$MAIN_HEADER" ]; then
    if ! grep -q "_WIN32_WINNT" "$MAIN_HEADER"; then
        sed -i '1i\
#ifdef _WIN32\
#ifndef _WIN32_WINNT\
#define _WIN32_WINNT 0x0501\
#endif\
#ifndef WINVER\
#define WINVER 0x0501\
#endif\
#endif' "$MAIN_HEADER"
        echo "  Added to $MAIN_HEADER"
    else
        echo "  Already defined in $MAIN_HEADER"
    fi
fi

echo ""
echo "=== Patching complete! ==="
echo "You can now run the build script (build-xp32.sh)"
