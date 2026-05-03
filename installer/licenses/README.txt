================================================================================
                        NeuralXP - License Information
================================================================================

NeuralXP is licensed under the GNU General Public License version 3 (GPL v3).
See: GPL-3.0.txt (this folder)

NeuralXP bundles the following third-party open-source components, each
under its own license. Full license texts are included in this folder.

--------------------------------------------------------------------------------

  Component                | License                          | File
  -------------------------+----------------------------------+--------------------------------
  NeuralXP (this software) | GPL v3                           | GPL-3.0.txt
  llama.cpp                | MIT                              | llama.cpp-LICENSE.txt
  ggml                     | MIT (same as llama.cpp)          | llama.cpp-LICENSE.txt
  cpp-httplib              | MIT                              | cpp-httplib-LICENSE.txt
  Qwen2.5-0.5B-Instruct    | Apache 2.0                       | Qwen2.5-LICENSE.txt
  Apache 2.0 (reference)   | Apache 2.0                       | Apache-2.0.txt
  GCC runtime libraries    | GPL v3 + Runtime Exception       | GCC-Runtime-Exception.txt
  MinGW-w64 runtime        | Public Domain / permissive       | (see mingw-w64.org)

--------------------------------------------------------------------------------

Notes:

 * The Qwen2.5 license file is the Apache 2.0 license text from the
   official Hugging Face repository.

 * GCC's runtime libraries (libgcc, libstdc++, libgomp) are statically
   linked into NeuralXP. The GCC Runtime Library Exception explicitly
   permits this combination without imposing GPL on the linked program.
   However, NeuralXP's own source code is GPL v3 by Panmox's choice.

 * Source code for NeuralXP is available at: https://panmox.org

--------------------------------------------------------------------------------

Copyright (c) 2026 Panmox (panmox.org)
