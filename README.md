# NeuralXP

**Offline AI assistant for Windows XP.** Built by [Panmox](https://panmox.org).

NeuralXP runs a small language model entirely offline on Windows XP hardware
(tested on a Dell Inspiron 6000 with a Pentium M and 2GB RAM).

- Native Win32 GUI (no .NET, no modern frameworks)
- Cross-compiled with MinGW-w64 i686 (MSVCRT runtime)
- Runs on real Pentium M (SSE2 only, no SSE3/AVX/BMI2)
- Includes a multi-chat system with auto-save
- Pure offline — no internet, no telemetry

![NeuralXP Screenshot](docs/screenshot.png)

## Status

Working on Windows XP SP2/SP3 32-bit. Speed: ~30 seconds per short answer
on a Pentium M @ 1.6 GHz.

## Components

| Folder | What |
|---|---|
| `gui/` | The NeuralXP GUI source (Win32 API) and headless inference helper |
| `llama-modifications/` | Modified llama.cpp example files (system prompt, output format) |
| `patches/` | Patches applied to llama.cpp/ggml for XP compatibility |
| `scripts/` | Build scripts (cross-compile, patch, download model) |
| `cmake/` | CMake toolchain file for XP 32-bit cross-compilation |
| `installer/` | NSIS installer script and bundled license files |

## Build From Source

### Prerequisites

- **Modern Windows** (10/11) with Git Bash or MSYS2
- **Git**
- **CMake** 3.14+
- **MinGW-w64 i686 GCC 10.5.0** with MSVCRT runtime
  ([winlibs.com](https://winlibs.com/) — pick the i686 + MSVCRT + DWARF build)
- **NSIS 3.x** (for building installer) — [nsis.sourceforge.io](https://nsis.sourceforge.io/)

### Build steps

```bash
# 1. Clone llama.cpp at a known-good commit
git clone https://github.com/ggerganov/llama.cpp.git
cd llama.cpp
git checkout master

# 2. Apply XP patches
bash ../scripts/patch-for-xp.sh

# 3. Copy our modified example files
cp -r ../llama-modifications/* .

# 4. Cross-compile for XP 32-bit
bash ../scripts/build-xp32.sh

# 5. Download the model
bash ../scripts/download-model.sh

# 6. Build the GUI
g++ -o NeuralXP.exe ../gui/ai-assistant.cpp \
    -static -mwindows -lcomctl32 -lcomdlg32 \
    -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 \
    -march=pentium-m -mno-sse3 -O2 -s

# 7. (Optional) Build the installer
makensis ../installer/NeuralXP-installer.nsi
```

### Why the specific GCC version?

GCC 15+ uses Vista+ APIs (`_putenv_s`) that don't exist on XP. GCC 10.5.0
+ MSVCRT runtime is the sweet spot. **Don't use UCRT** — XP doesn't have it.

## Why Pentium M / SSE2 only?

Pentium M predates SSE3, SSSE3, SSE4, AVX, and BMI2. You must compile with:

```
-march=pentium-m -mno-sse3
```

And explicitly disable BMI2 in CMake:

```
-DGGML_BMI2=OFF -DGGML_CPU_ALL_VARIANTS=OFF
```

Otherwise the binary crashes with "illegal instruction" on real XP hardware.

## License

NeuralXP is licensed under the **GNU General Public License v3.0**.
See [LICENSE](LICENSE) for the full text.

### Third-party components

| Component | License |
|---|---|
| llama.cpp / ggml | MIT |
| cpp-httplib | MIT |
| Qwen2.5-0.5B-Instruct (model) | Apache 2.0 |
| GCC runtime (libgcc, libstdc++, libgomp) | GPL v3 + Runtime Exception |
| MinGW-w64 runtime | Public Domain / permissive |

Full license texts are in [`installer/licenses/`](installer/licenses/).

## Releases

Pre-built installers are not in the repo (the model alone is 415 MB).
Get them from the [Releases](https://github.com/panmauk/NeuralXP/releases) page.

## Credits

- **Panmox** — NeuralXP itself ([panmox.org](https://panmox.org))
- **ggml authors** — llama.cpp and ggml
- **Alibaba Cloud / Qwen Team** — Qwen2.5 model
- **yhirose** — cpp-httplib

---

*Built because old hardware deserves modern AI too.*
