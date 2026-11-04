#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace myvoice {

// 由 myVoice 自身查找内置的 RVC / Seed-VC 源码；用户显式路径始终优先。
// 不下载源码、不调用 git、也不依赖 Git 子模块。
inline std::filesystem::path resolve_engine_root(
    const std::filesystem::path& configured,
    const char* folder,
    const std::filesystem::path& marker) {
    namespace fs = std::filesystem;
    if (!configured.empty()) return configured;

    std::vector<fs::path> anchors;
    std::error_code ec;
    auto cwd = fs::current_path(ec);
    if (!ec) anchors.push_back(cwd);
#ifdef __linux__
    // Linux: 从任何工作目录启动可执行程序时，也能按程序所在目录向上查找。
    ec.clear();
    const auto executable = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) anchors.push_back(executable.parent_path());
#endif
#ifdef MYVOICE_SOURCE_ROOT
    // CMake 构建的程序可从任何工作目录直接启动。
    anchors.emplace_back(fs::u8path(MYVOICE_SOURCE_ROOT));
#endif

    for (const auto& anchor : anchors) {
        fs::path parent = anchor;
        for (int i = 0; i < 16; ++i) {
            const fs::path candidate = parent / "third_party" / folder;
            ec.clear();
            if (fs::is_regular_file(candidate / marker, ec) && !ec)
                return fs::canonical(candidate);
            if (parent == parent.root_path() || parent == parent.parent_path()) break;
            parent = parent.parent_path();
        }
    }
    throw std::invalid_argument(
        std::string("Bundled ") + folder + " source not found (third_party/" +
        folder + "). Ensure myVoice is checked out completely, or use /" +
        (std::string(folder) == "rvc" ? "rvc-root" : "seed-root") + " to override.");
}

} // namespace myvoice
