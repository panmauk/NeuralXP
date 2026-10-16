#!/bin/bash
# build-linux.sh - Build NeuralXP for Linux and package it (.deb + AppImage).
#
#   bash linux/build-linux.sh [x64|arm64|all]      (default: all)
#
# Output (linux/dist/):
#   neuralxp_<ver>_<arch>.deb          full: model bundled, works offline out of the box
#   neuralxp-lite_<ver>_<arch>.deb     lite: downloads the model on first run
#   NeuralXP-<ver>-<arch>.AppImage / NeuralXP-Lite-<ver>-<arch>.AppImage
#
# Toolchain (no root needed; created by linux/toolchain/*.sh):
#   SYSROOT_X64 / SYSROOT_ARM64  Ubuntu 22.04 (jammy) GTK3 sysroots -> glibc 2.35 / GTK 3.24.33 baseline,
#                                so the packages run on Ubuntu 22.04+, Debian 12+, Mint 21+, Fedora 36+
#   TC                           Ubuntu 22.04 GCC 12 wrappers: {x86_64,aarch64}-linux-gnu-{gcc,g++,ar,ranlib,strip}
#                                (jammy compiler + jammy libstdc++ = no symbols newer than glibc 2.35)
#   LLAMA_DIR                    llama.cpp checkout (cloned at LLAMA_COMMIT if missing)
#   MODEL                        path to qwen2.5-0.5b-instruct-q2_k.gguf (downloaded + verified if missing)
#   APPIMAGETOOL, RUNTIME_DIR    appimagetool and type2 runtimes (runtime-x86_64 / runtime-aarch64)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLS="${TOOLS:-$HOME/lvtools}"
SYSROOT_X64="${SYSROOT_X64:-$TOOLS/sysroot-jammy-amd64}"
SYSROOT_ARM64="${SYSROOT_ARM64:-$TOOLS/sysroot-jammy-arm64}"
TC="${TC:-$TOOLS/jammy-tc/bin}"   # Ubuntu 22.04 GCC 12 (native + aarch64 cross) wrappers
LLAMA_DIR="${LLAMA_DIR:-$TOOLS/llama.cpp}"
LLAMA_COMMIT="${LLAMA_COMMIT:-e7268bf}"
MODEL="${MODEL:-$TOOLS/models/qwen2.5-0.5b-instruct-q2_k.gguf}"
MODEL_URL="https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q2_k.gguf"
MODEL_SHA256="9ee36184e616dfc76df4f5dd66f908dbde6979524ae36e6cefb67f532f798cb8"
APPIMAGETOOL="${APPIMAGETOOL:-$TOOLS/appimagetool-x86_64.AppImage}"
RUNTIME_DIR="${RUNTIME_DIR:-$TOOLS}"
WORK="${WORK:-$HOME/build/neuralxp}"
DIST="$SCRIPT_DIR/dist"
VERSION="1.0.1"
JOBS="$(nproc)"

TARGET="${1:-all}"
case "$TARGET" in x64) ARCHES="x64" ;; arm64) ARCHES="arm64" ;; all) ARCHES="x64 arm64" ;;
    *) echo "usage: $0 [x64|arm64|all]"; exit 1 ;; esac

log() { echo -e "\n==> $*"; }

# ---------- sources ----------
if [ ! -d "$LLAMA_DIR/.git" ]; then
    log "Cloning llama.cpp ($LLAMA_COMMIT)"
    git clone -q https://github.com/ggml-org/llama.cpp.git "$LLAMA_DIR"
    git -C "$LLAMA_DIR" checkout -q "$LLAMA_COMMIT"
fi
log "llama.cpp: $(git -C "$LLAMA_DIR" log --oneline -1)"

if ! echo "$MODEL_SHA256  $MODEL" | sha256sum -c --status 2>/dev/null; then
    log "Downloading model"
    mkdir -p "$(dirname "$MODEL")"
    curl -fL -C - -o "$MODEL" "$MODEL_URL"
    echo "$MODEL_SHA256  $MODEL" | sha256sum -c
fi

mkdir -p "$WORK" "$DIST"

# ---------- icons (generated with the project's own gui/make-icon.py) ----------
ICONS="$WORK/icons"
if [ ! -f "$ICONS/256.png" ]; then
    log "Generating icons"
    mkdir -p "$ICONS"
    python3 -I - "$PROJECT_DIR/gui/make-icon.py" "$ICONS" <<'PY'
import importlib.util, sys
spec = importlib.util.spec_from_file_location("mkicon", sys.argv[1])
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
for s in (16, 32, 48, 64, 128, 256):
    m.make_icon(s).save(f"{sys.argv[2]}/{s}.png")
PY
fi

# ---------- build one architecture ----------
build_arch() {
    local A="$1" DEB_ARCH APPIMAGE_ARCH SYSROOT CC CXX TRIPLET PROC
    if [ "$A" = x64 ]; then
        DEB_ARCH=amd64; APPIMAGE_ARCH=x86_64; SYSROOT="$SYSROOT_X64"; TRIPLET=x86_64-linux-gnu; PROC=x86_64
    else
        DEB_ARCH=arm64; APPIMAGE_ARCH=aarch64; SYSROOT="$SYSROOT_ARM64"; TRIPLET=aarch64-linux-gnu; PROC=aarch64
    fi
    CC="$TC/$TRIPLET-gcc"; CXX="$TC/$TRIPLET-g++"
    [ -d "$SYSROOT/usr/include/gtk-3.0" ] || { echo "missing sysroot $SYSROOT"; exit 1; }
    local B="$WORK/$A"
    mkdir -p "$B"

    cat > "$B/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $PROC)
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_SYSROOT $SYSROOT)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
    echo "set(CMAKE_AR $TC/$TRIPLET-ar CACHE FILEPATH \"\")
set(CMAKE_RANLIB $TC/$TRIPLET-ranlib CACHE FILEPATH \"\")
set(CMAKE_STRIP $TC/$TRIPLET-strip CACHE FILEPATH \"\")" >> "$B/toolchain.cmake"

    # Ubuntu 22.04's GCC 12 does not know -march=armv9.2-a+sme (needs GCC 14). Drop the two SME
    # variants; SME CPUs (rare) then use the armv8.6 (SVE2 + i8mm) variant instead.
    if [ "$A" = arm64 ] && [ "$("$CXX" -dumpversion | cut -d. -f1)" -lt 14 ]; then
        sed -i -E 's|^(\s*)(ggml_add_cpu_backend_variant\(armv9\.2_[12] .*)$|\1# \2  # NeuralXP: disabled, needs GCC 14|' \
            "$LLAMA_DIR/ggml/src/CMakeLists.txt"
    fi

    log "[$A] Building inference helper + llama.cpp"
    # GGML_BACKEND_DL + GGML_CPU_ALL_VARIANTS: one libggml-cpu-<variant>.so per CPU generation,
    # the best one is picked at runtime (fast on new CPUs, still runs on old ones).
    cmake -S "$PROJECT_DIR/cmake/helper" -B "$B/helper" -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE="$B/toolchain.cmake" \
        -DLLAMA_DIR="$LLAMA_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DGGML_NATIVE=OFF \
        -DGGML_BACKEND_DL=ON \
        -DGGML_CPU_ALL_VARIANTS=ON \
        -DGGML_OPENMP=OFF \
        -DCMAKE_SKIP_BUILD_RPATH=OFF \
        -DCMAKE_BUILD_RPATH_USE_ORIGIN=ON \
        -DCMAKE_BUILD_RPATH='$ORIGIN' \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc -Wl,-rpath,\$ORIGIN" \
        -DCMAKE_SHARED_LINKER_FLAGS="-static-libstdc++ -static-libgcc -Wl,-rpath,\$ORIGIN" \
        -DCMAKE_MODULE_LINKER_FLAGS="-static-libstdc++ -static-libgcc -Wl,-rpath,\$ORIGIN" \
        > "$B/cmake.log"
    # "all": the helper, libllama/libggml and every libggml-cpu-<variant>.so
    cmake --build "$B/helper" -j"$JOBS" > "$B/build.log" 2>&1 || { tail -40 "$B/build.log"; exit 1; }

    log "[$A] Building GUI"
    local PKG_ENV="PKG_CONFIG_SYSROOT_DIR=$SYSROOT PKG_CONFIG_LIBDIR=$SYSROOT/usr/lib/$TRIPLET/pkgconfig:$SYSROOT/usr/share/pkgconfig"
    local GTK_FLAGS; GTK_FLAGS="$(env $PKG_ENV pkg-config --cflags --libs gtk+-3.0)"
    $CC --sysroot="$SYSROOT" -O2 -Wall -Wextra -o "$B/neuralxp" "$PROJECT_DIR/linux/neuralxp-gtk.c" $GTK_FLAGS

    # ---------- stage /opt/neuralxp ----------
    local STAGE="$B/stage"
    rm -rf "$STAGE"; mkdir -p "$STAGE/bin"
    cp "$B/neuralxp" "$STAGE/bin/"
    find "$B/helper" -name llama-simple-chat-headless -type f -perm -u+x -exec cp {} "$STAGE/bin/" \;
    find "$B/helper" \( -name 'libllama.so*' -o -name 'libggml*.so*' \) \( -type f -o -type l \) -exec cp -P {} "$STAGE/bin/" \;
    local STRIP="$TC/$TRIPLET-strip"
    find "$STAGE/bin" -type f -exec $STRIP --strip-unneeded {} \; 2>/dev/null || true
    # Fail if anything needs a newer glibc than the Ubuntu 22.04 baseline (would not load on older distros)
    local f bad=0
    for f in "$STAGE"/bin/*; do
        [ -L "$f" ] && continue
        local v; v="$("$TC/$TRIPLET-objdump" -T "$f" | grep -o 'GLIBC_2\.[0-9]*' | sort -uV | tail -1)"
        if [ "$(printf '%s\nGLIBC_2.35\n' "$v" | sort -V | tail -1)" != "GLIBC_2.35" ]; then
            echo "ERROR: $(basename "$f") needs $v (> GLIBC_2.35)"; bad=1
        fi
        if "$TC/$TRIPLET-objdump" -p "$f" | grep -q 'NEEDED.*libstdc++'; then
            echo "ERROR: $(basename "$f") links libstdc++ dynamically"; bad=1
        fi
    done
    [ $bad = 0 ] || exit 1
    mkdir -p "$STAGE/licenses"
    cp "$PROJECT_DIR/LICENSE" "$STAGE/licenses/NeuralXP-GPL-3.0.txt"
    cp "$PROJECT_DIR/installer/licenses/llama.cpp-LICENSE.txt" "$PROJECT_DIR/installer/licenses/Qwen2.5-LICENSE.txt" \
       "$PROJECT_DIR/installer/licenses/Apache-2.0.txt" "$STAGE/licenses/"

    for VARIANT in full lite; do
        make_deb "$A" "$DEB_ARCH" "$STAGE" "$VARIANT"
        make_appimage "$A" "$APPIMAGE_ARCH" "$STAGE" "$VARIANT"
    done
}

desktop_file() {  # $1 = Exec
cat <<EOF
[Desktop Entry]
Type=Application
Name=NeuralXP
GenericName=Offline AI Assistant
Comment=Offline AI assistant - runs a small language model locally, no internet needed
Exec=$1
Icon=neuralxp
Terminal=false
Categories=Utility;
Keywords=AI;chat;LLM;offline;assistant;
StartupWMClass=neuralxp
EOF
}

make_deb() {
    local A="$1" DEB_ARCH="$2" STAGE="$3" VARIANT="$4"
    local PKG=neuralxp OTHER=neuralxp-lite
    [ "$VARIANT" = lite ] && { PKG=neuralxp-lite; OTHER=neuralxp; }
    log "[$A] .deb $PKG"
    local R="$WORK/$A/deb-$VARIANT"
    rm -rf "$R"; mkdir -p "$R/DEBIAN" "$R/opt/neuralxp" "$R/usr/bin" "$R/usr/share/applications" "$R/usr/share/doc/$PKG"
    cp -a "$STAGE/bin" "$R/opt/neuralxp/"
    if [ "$VARIANT" = full ]; then
        mkdir -p "$R/opt/neuralxp/models"
        cp "$MODEL" "$R/opt/neuralxp/models/"
    fi
    ln -s /opt/neuralxp/bin/neuralxp "$R/usr/bin/neuralxp"
    desktop_file neuralxp > "$R/usr/share/applications/neuralxp.desktop"
    for s in 16 32 48 64 128 256; do
        mkdir -p "$R/usr/share/icons/hicolor/${s}x${s}/apps"
        cp "$ICONS/$s.png" "$R/usr/share/icons/hicolor/${s}x${s}/apps/neuralxp.png"
    done
    cp "$STAGE"/licenses/* "$R/usr/share/doc/$PKG/"
    cat > "$R/usr/share/doc/$PKG/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: NeuralXP
Source: https://github.com/panmauk/NeuralXP

Files: *
Copyright: 2026 Panmox
License: GPL-3.0+
 See /usr/share/doc/$PKG/NeuralXP-GPL-3.0.txt

Files: opt/neuralxp/bin/libllama.so* opt/neuralxp/bin/libggml*
Copyright: The ggml authors
License: MIT
 See /usr/share/doc/$PKG/llama.cpp-LICENSE.txt

Files: opt/neuralxp/models/*
Copyright: Alibaba Cloud (Qwen Team)
License: Apache-2.0
 See /usr/share/doc/$PKG/Apache-2.0.txt and Qwen2.5-LICENSE.txt
EOF
    local DEPENDS="libc6 (>= 2.35), libgtk-3-0 (>= 3.22), libglib2.0-0 (>= 2.56), libpango-1.0-0, libcairo2"
    local DESC_EXTRA=" The AI model (Qwen2.5-0.5B, Apache 2.0) is included."
    if [ "$VARIANT" = lite ]; then
        DEPENDS="$DEPENDS, curl | wget"
        DESC_EXTRA=" Lite package: the AI model (Qwen2.5-0.5B, 396 MB) is downloaded on first start."
    fi
    local SIZE; SIZE="$(du -sk "$R" | cut -f1)"
    cat > "$R/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Architecture: $DEB_ARCH
Maintainer: Panmox <panmauk@users.noreply.github.com>
Installed-Size: $SIZE
Depends: $DEPENDS
Conflicts: $OTHER
Replaces: $OTHER
Provides: neuralxp-app
Section: utils
Priority: optional
Homepage: https://panmox.org
Description: Offline AI assistant (NeuralXP)
 NeuralXP runs a small language model entirely on your own computer.
 No internet, no account, no telemetry. Built by Panmox.
$DESC_EXTRA
EOF
    cat > "$R/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi
exit 0
EOF
    cat > "$R/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = remove ] || [ "$1" = purge ]; then
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi
exit 0
EOF
    chmod 755 "$R/DEBIAN/postinst" "$R/DEBIAN/postrm"
    find "$R" -path "$R/DEBIAN" -prune -o -type d -exec chmod 755 {} \;
    find "$R/usr" "$R/opt" -type f -exec chmod 644 {} \;
    chmod 755 "$R/opt/neuralxp/bin/neuralxp" "$R/opt/neuralxp/bin/llama-simple-chat-headless"
    find "$R/opt/neuralxp/bin" -name '*.so*' -type f -exec chmod 644 {} \;
    dpkg-deb --root-owner-group -Zxz --build "$R" "$DIST/${PKG}_${VERSION}_${DEB_ARCH}.deb" >/dev/null
}

make_appimage() {
    local A="$1" AARCH="$2" STAGE="$3" VARIANT="$4"
    local NAME="NeuralXP"; [ "$VARIANT" = lite ] && NAME="NeuralXP-Lite"
    log "[$A] AppImage $NAME"
    local D="$WORK/$A/AppDir-$VARIANT"
    rm -rf "$D"; mkdir -p "$D/usr/lib/neuralxp"
    cp -a "$STAGE/bin" "$D/usr/lib/neuralxp/"
    cp -a "$STAGE/licenses" "$D/usr/lib/neuralxp/"
    if [ "$VARIANT" = full ]; then
        mkdir -p "$D/usr/lib/neuralxp/models"
        cp "$MODEL" "$D/usr/lib/neuralxp/models/"
    fi
    cat > "$D/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/lib/neuralxp/bin/neuralxp" "$@"
EOF
    chmod 755 "$D/AppRun"
    desktop_file neuralxp > "$D/neuralxp.desktop"
    cp "$ICONS/256.png" "$D/neuralxp.png"
    mkdir -p "$D/usr/share/icons/hicolor/256x256/apps"
    cp "$ICONS/256.png" "$D/usr/share/icons/hicolor/256x256/apps/neuralxp.png"
    ARCH="$AARCH" APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" --no-appstream \
        --runtime-file "$RUNTIME_DIR/runtime-$AARCH" "$D" "$DIST/$NAME-$VERSION-$AARCH.AppImage" > "$WORK/$A/appimage-$VARIANT.log" 2>&1 \
        || { tail -20 "$WORK/$A/appimage-$VARIANT.log"; exit 1; }
}

for A in $ARCHES; do build_arch "$A"; done

( cd "$DIST" && sha256sum *.deb *.AppImage > SHA256SUMS )
log "Done:"
ls -lh "$DIST"
