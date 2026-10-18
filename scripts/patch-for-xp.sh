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
    # keep the first backup: on re-runs httplib.h is already the downgraded copy
    [ -f vendor/cpp-httplib/httplib.h.backup ] || cp vendor/cpp-httplib/httplib.h vendor/cpp-httplib/httplib.h.backup
    curl -sL https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h \
        -o vendor/cpp-httplib/httplib.h
    echo "  Done. Original backed up to httplib.h.backup"
elif [ -f "examples/server/httplib.h" ]; then
    [ -f examples/server/httplib.h.backup ] || cp examples/server/httplib.h examples/server/httplib.h.backup
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

PATCHED=0

# 3a. ggml-cpu threadpool: current llama.cpp uses SRWLOCK + CONDITION_VARIABLE +
#     SleepConditionVariableSRW (all Vista+). The old generic sed below turned
#     CONDITION_VARIABLE_LOCKMODE_SHARED into "HANDLE_LOCKMODE_SHARED" (compile error),
#     left SleepConditionVariableSRW unpatched, and its CS-wait replacement waited while
#     still holding the lock (deadlock). Replace the whole block with patches/ggml-xp-threading.h
#     (CRITICAL_SECTION + an XP-safe condition variable).
CPU_C="ggml/src/ggml-cpu/ggml-cpu.c"
if [ -f "$CPU_C" ] && grep -q "typedef CONDITION_VARIABLE ggml_cond_t;" "$CPU_C"; then
    echo "  Patching $CPU_C (threadpool sync -> XP-safe CRITICAL_SECTION + condition variable)..."
    cp "$(dirname "$0")/../patches/ggml-xp-threading.h" ggml/src/ggml-cpu/
    perl -0pi -e 's{typedef CONDITION_VARIABLE ggml_cond_t;.*?#define ggml_cond_broadcast\(c\) WakeAllConditionVariable\(c\)}{#include "ggml-xp-threading.h"}s' "$CPU_C"
    PATCHED=$((PATCHED + 1))
fi

# 3a2. ggml.c timer init uses InitOnceExecuteOnce / PINIT_ONCE (Vista+). Replace it with a
#      one-time init built on InterlockedCompareExchange (available on every NT version).
GGML_C="ggml/src/ggml.c"
if [ -f "$GGML_C" ] && grep -q "InitOnceExecuteOnce" "$GGML_C"; then
    echo "  Patching $GGML_C (InitOnceExecuteOnce -> XP-safe one-time init)..."
    perl -0pi -e '
        s{static BOOL CALLBACK ggml_time_init_once\(PINIT_ONCE once, PVOID param, PVOID \*ctx\) \{\s*UNUSED\(once\);\s*UNUSED\(param\);\s*UNUSED\(ctx\);}{static void ggml_time_init_once(void) \{}s;
        s{(QueryPerformanceCounter\(&t\);\s*timer_start = t\.QuadPart;\s*)return TRUE;\s*\}\s*void ggml_time_init\(void\) \{\s*static INIT_ONCE once = INIT_ONCE_STATIC_INIT;\s*InitOnceExecuteOnce\(&once, ggml_time_init_once, NULL, NULL\);\s*\}}{$1\}\nvoid ggml_time_init(void) \{\n    // XP: no InitOnce API. 0 = not started, 1 = running, 2 = done\n    static volatile LONG once_state = 0;\n    if (InterlockedCompareExchange(&once_state, 1, 0) == 0) \{\n        ggml_time_init_once();\n        InterlockedExchange(&once_state, 2);\n    \} else \{\n        while (once_state != 2) Sleep(0);\n    \}\n\}}s;
    ' "$GGML_C"
    grep -q "InitOnceExecuteOnce" "$GGML_C" && { echo "  ERROR: could not patch InitOnceExecuteOnce in $GGML_C"; exit 1; }
    PATCHED=$((PATCHED + 1))
fi

# 3a3. 32-bit only: llama-model-loader stores 4 GiB in a size_t, which wraps to 0 on i686, so the
#      "lazy read" path was enabled for every tensor instead of only for > 4 GiB ones. Use 64-bit.
LOADER="src/llama-model-loader.cpp"
if [ -f "$LOADER" ] && grep -q "constexpr size_t auto_min_size = 4ull" "$LOADER"; then
    echo "  Patching $LOADER (32-bit size_t overflow in auto_min_size)..."
    sed -i 's/constexpr size_t auto_min_size = 4ull/constexpr uint64_t auto_min_size = 4ull/' "$LOADER"
    PATCHED=$((PATCHED + 1))
fi

# 3b. Anything else: SRWLOCK -> CRITICAL_SECTION (generic fallback for other files / older trees)
GGML_FILES=$(find . -path ./vendor -prune -o \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) ! -name ggml-xp-threading.h -print | head -500)

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
        sed -i 's/\bCONDITION_VARIABLE\b/HANDLE/g' "$f"
        sed -i 's/InitializeConditionVariable(\([^)]*\))/(*(\1) = CreateEvent(NULL, TRUE, FALSE, NULL))/g' "$f"
        # Must release the lock while waiting (the old replacement kept holding it -> deadlock).
        # Manual-reset event: a SetEvent between Leave and Wait is not lost.
        sed -i 's/SleepConditionVariable\(CS\|SRW\)(\([^,]*\),\([^,]*\),[^)]*)/(LeaveCriticalSection(\3), WaitForSingleObject(*(\2), INFINITE), EnterCriticalSection(\3), ResetEvent(*(\2)), TRUE)/g' "$f"
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
