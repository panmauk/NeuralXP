# NeuralXP for Linux

GTK 3 port of the NeuralXP GUI (`neuralxp-gtk.c`): same layout, menus, shortcuts, chat system and
XP Luna look as the Win32 version, running the same inference helper (`gui/simple-chat-headless.cpp`)
built against native llama.cpp.

## Packages

`build-linux.sh` produces, for **x86_64** and **arm64**:

| File | What |
|---|---|
| `neuralxp_<ver>_<arch>.deb` | Full: model bundled, works offline right after install |
| `neuralxp-lite_<ver>_<arch>.deb` | Lite: downloads the model (396 MB, SHA-256 verified) on first start |
| `NeuralXP-<ver>-<arch>.AppImage` | Full, portable |
| `NeuralXP-Lite-<ver>-<arch>.AppImage` | Lite, portable |

Built against Ubuntu 22.04 (glibc 2.35, GTK 3.24) so they run on Ubuntu 22.04+, Debian 12+,
Linux Mint 21+, Fedora 36+ and similar. llama.cpp is built with one CPU backend per CPU generation
(`GGML_CPU_ALL_VARIANTS`); the fastest one your CPU supports is picked at runtime.

Install: `sudo apt install ./neuralxp_1.0.3_amd64.deb`, or `chmod +x NeuralXP-*.AppImage` and run it.
The AppImages use the system's GTK 3 (installed on practically every desktop).

## Where things live

- Program: `/opt/neuralxp/bin` (launcher `/usr/bin/neuralxp`)
- Model: `/opt/neuralxp/models/` (full) or `~/.local/share/neuralxp/models/` (lite)
- Chats: `~/.local/share/neuralxp/chats/` (same text format as the Windows version, so chats can be copied over)
- `NEURALXP_MODEL=/path/model.gguf` overrides the model

## Building

```bash
bash linux/build-linux.sh          # both architectures
bash linux/build-linux.sh x64      # or one
```

Needs Ubuntu 22.04 GTK sysroots and GCC 12 toolchains; `linux/toolchain/make-sysroot.sh` (run for `amd64` and `arm64`) and
`linux/toolchain/make-jammy-toolchain.sh` create them in `~/lvtools` without root. See the variables
at the top of `build-linux.sh`.
