# Vendored FTXUI

This directory contains a source copy of **FTXUI v7.0.3** by Arthur Sonzogni and contributors.
Upstream: https://github.com/ArthurSonzogni/FTXUI  |  tag `v7.0.3`

`include/` and `src/` are copied from that tag, excluding unit tests, fuzzers, examples, docs and C++ modules.
`LICENSE` retains the original MIT license. `CMakeLists.txt` is a minimal offline build file authored for myVoice.
No git submodule, FetchContent, installed FTXUI package or network access is required by myVoice's CMake configure/build.
