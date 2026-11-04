#include "myvoice/VoiceTrainer.h"
#include "Process.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace myvoice {
namespace fs = std::filesystem;
namespace {
fs::path absolute_executable(fs::path exe) {
    if (exe.empty()) throw std::invalid_argument("Python executable not set");
    return exe.has_parent_path() ? fs::absolute(exe) : exe;
}
std::string read_tail(const fs::path& p) {
    std::ifstream file(p, std::ios::binary);
    if (!file) return {};
    file.seekg(0, std::ios::end);
    const std::streamoff end = file.tellg();
    if (end <= 0) return {};
    file.seekg(std::max<std::streamoff>(0, end - 2500));
    return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}  // namespace
VoiceTrainer::VoiceTrainer(TrainingConfiguration cfg) : configuration_(std::move(cfg)) {}
TrainingResult VoiceTrainer::train(const TrainingRequest& req) const {
    const auto started = std::chrono::steady_clock::now();
    if (!std::regex_match(req.name, std::regex("[A-Za-z0-9_-]{1,64}")))
        throw std::invalid_argument("Training name must contain only letters, digits, - or _ (1..64)");
    if (req.epochs < 1 || req.epochs > 10000 || req.batch_size < 1 || req.batch_size > 128 ||
        req.workers < 1 || req.workers > 64 || req.save_every < 1 || req.save_every > req.epochs ||
        req.gpu < 0 || req.gpu > 31 || (req.device != "cuda" && req.device != "cpu"))
        throw std::invalid_argument("Invalid training options; see /help");
    if (configuration_.timeout <= std::chrono::milliseconds::zero())
        throw std::invalid_argument("Training timeout must be positive");
    if (!fs::is_directory(configuration_.rvc_root))
        throw std::invalid_argument("RVC root is not a directory; use /rvc-root");
    if (!fs::is_directory(req.dataset))
        throw std::invalid_argument("Training dataset is not a directory; use /dataset");
    const fs::path root = fs::canonical(configuration_.rvc_root);
    const fs::path dataset = fs::canonical(req.dataset);
    fs::path script = configuration_.bridge_script;
#ifdef MYVOICE_TRAIN_BRIDGE_SCRIPT
    if (script.empty()) script = fs::u8path(MYVOICE_TRAIN_BRIDGE_SCRIPT);
#endif
    // VS2022 原生 .sln 无 CMake 路径宏；在项目目录或工作目录上溯查找本仓库的桥接脚本。
    // /train-script 始终优先，避免从其他项目目录误加载外部代码。
    if (script.empty()) {
        fs::path parent = fs::current_path();
        for (int depth = 0; depth < 8; ++depth) {
            const fs::path candidate = parent / "scripts" / "rvc_train.py";
            if (fs::is_regular_file(candidate)) {
                script = candidate;
                break;
            }
            if (parent == parent.parent_path()) break;
            parent = parent.parent_path();
        }
    }
    if (script.empty() || !fs::is_regular_file(script))
        throw std::invalid_argument("Missing RVC training bridge (scripts/rvc_train.py)");
    script = fs::canonical(script);
    if (!fs::is_regular_file(root / "train" / "train.py"))
        throw std::invalid_argument("Invalid RVC installation: train/train.py missing");
    const auto exp = root / "logs" / req.name;
    fs::create_directories(exp);
    const auto logfile = exp / "myvoice_train.log";
    const std::vector<fs::path> args = {
        script, "--rvc-root", root, "--dataset", dataset, "--name", req.name,
        "--epochs", std::to_string(req.epochs), "--batch", std::to_string(req.batch_size),
        "--save-every", std::to_string(req.save_every), "--workers", std::to_string(req.workers),
        "--device", req.device, "--gpu", std::to_string(req.gpu),
        "--index", req.build_index ? "on" : "off"
    };
    int code = 0;
    try {
        code = detail::run_process(absolute_executable(configuration_.python), args,
                                   root, configuration_.timeout, logfile);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("RVC training error: ") + ex.what() + "\n" + read_tail(logfile));
    }
    if (code != 0)
        throw std::runtime_error("RVC training exited with " + std::to_string(code) + "\n" + read_tail(logfile));
    return {exp, logfile, std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started)};
}
}  // namespace myvoice
