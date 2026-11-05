#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#endif

namespace myvoice {

// 使用仓库对应 Release 中解压的便携 Python。调用方显式配置 /python 时优先使用。
inline std::filesystem::path resolve_runtime_python(
    const std::filesystem::path& configured, const std::string& engine) {
    namespace fs = std::filesystem;
    if (!configured.empty()) return configured;
    if (engine != "seed-vc" && engine != "rvc")
        throw std::invalid_argument("Unknown portable Python engine");
#ifdef _WIN32
    const fs::path relative = fs::path("runtime") / "windows-x64" / engine / "python.exe";
#else
    const fs::path relative = fs::path("runtime") / "linux-x64" / engine / "bin" / "python3";
#endif
    // 用户执行 /gpu-setup 并通过真实 GPU 分配测试后，才优先使用独立的 GPU 环境。
#ifdef _WIN32
    const fs::path gpu_root = relative.parent_path().parent_path() / (engine + "-gpu");
    const fs::path gpu_python = gpu_root / "python.exe";
#else
    // Linux 多一层 bin/；必须回到平台根目录，否则会误查 rvc/rvc-gpu。
    const fs::path gpu_root = relative.parent_path().parent_path().parent_path() /
                              (engine + "-gpu");
    const fs::path gpu_python = gpu_root / "bin" / "python3";
#endif
    std::vector<fs::path> anchors;
    std::error_code ec;
    const auto cwd = fs::current_path(ec);
    if (!ec) anchors.push_back(cwd);
#ifdef _WIN32
    std::wstring exe(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (n > 0 && n < exe.size()) {
        exe.resize(n);
        anchors.push_back(fs::path(exe).parent_path());
    }
#elif defined(__linux__)
    ec.clear();
    const auto exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) anchors.push_back(exe.parent_path());
#endif
#ifdef MYVOICE_SOURCE_ROOT
    anchors.emplace_back(fs::u8path(MYVOICE_SOURCE_ROOT));
#endif

    for (const auto& base : anchors) {
        fs::path cursor = base;
        for (int i = 0; i < 16; ++i) {
            ec.clear();
            if (fs::is_regular_file(cursor / gpu_root / "myvoice-gpu-ready.json", ec) &&
                fs::is_regular_file(cursor / gpu_python, ec) && !ec)
                return fs::canonical(cursor / gpu_python);
            ec.clear();
            if (fs::is_regular_file(cursor / relative, ec) && !ec)
                return fs::canonical(cursor / relative);
            if (cursor == cursor.root_path() || cursor == cursor.parent_path()) break;
            cursor = cursor.parent_path();
        }
    }
    throw std::invalid_argument(
        "No bundled Python runtime for " + engine + ". Expected runtime/" +
#ifdef _WIN32
        std::string("windows-x64/")
#else
        std::string("linux-x64/")
#endif
        + engine + ". Download the CPU portable runtime from myVoice GitHub Releases "
        "and extract it into the repository root; or set /python explicitly.");
}
} // namespace myvoice
