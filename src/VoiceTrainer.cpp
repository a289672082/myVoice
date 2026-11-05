#include "myvoice/VoiceTrainer.h"
#include "myvoice/PythonRuntime.h"
#include "myvoice/EnginePaths.h"
#include "myvoice/ModelRegistry.h"
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

fs::path project_script(const fs::path& configured, const char* filename) {
    if (!configured.empty()) return fs::canonical(configured);
#ifdef MYVOICE_SOURCE_ROOT
    const fs::path known = fs::u8path(MYVOICE_SOURCE_ROOT) / "scripts" / filename;
    if (fs::is_regular_file(known)) return fs::canonical(known);
#endif
#ifdef MYVOICE_TRAIN_BRIDGE_SCRIPT
    const fs::path sibling = fs::u8path(MYVOICE_TRAIN_BRIDGE_SCRIPT).parent_path() / filename;
    if (fs::is_regular_file(sibling)) return fs::canonical(sibling);
#endif
    fs::path cursor = fs::current_path();
    for (int i = 0; i < 16; ++i) {
        const auto target = cursor / "scripts" / filename;
        if (fs::is_regular_file(target)) return fs::canonical(target);
        if (cursor == cursor.parent_path()) break;
        cursor = cursor.parent_path();
    }
    throw std::invalid_argument(std::string("Missing myVoice script scripts/") + filename);
}
}  // namespace
VoiceTrainer::VoiceTrainer(TrainingConfiguration cfg) : configuration_(std::move(cfg)) {}
TrainingResult VoiceTrainer::train(const TrainingRequest& req) const {
    const auto started = std::chrono::steady_clock::now();
    if (!std::regex_match(req.name, std::regex("[A-Za-z0-9_-]{1,64}")))
        throw std::invalid_argument("Training name must be alphanumeric, - or _ (1..64)");
    if (req.epochs < 1 || req.epochs > 10000 || req.steps < 1 || req.steps > 1000000 ||
        req.batch_size < 0 || req.batch_size > 128 || req.workers < 0 || req.workers > 64 ||
        req.save_every < 1 || req.save_every > 10000 || req.gpu < -1 || req.gpu > 31 ||
        (req.device != "auto" && req.device != "cuda" && req.device != "cpu"))
        throw std::invalid_argument("Invalid training options; see /help");
    if (configuration_.timeout <= std::chrono::milliseconds::zero())
        throw std::invalid_argument("Training timeout must be positive");
    const bool seed = req.engine == TrainingEngine::SeedVC;
    if (seed && !req.smart)
        throw std::invalid_argument("Seed-VC requires the smart fine-tuning pipeline");
    const std::string engine = seed ? "seed-vc" : "rvc";
    const fs::path selected = seed
        ? resolve_engine_root(configuration_.seed_vc_root, "seed-vc", "train_v2.py")
        : resolve_engine_root(configuration_.rvc_root, "rvc", "train/train.py");
    if (!fs::is_directory(req.dataset))
        throw std::invalid_argument("Training dataset directory not found; use /dataset");
    const auto root = fs::canonical(selected);
    const auto dataset = fs::canonical(req.dataset);
    const auto exp = root / (seed ? "runs" : "logs") / req.name;
    fs::create_directories(exp);
    const auto logfile = exp / "myvoice_train.log";
    std::vector<fs::path> args;
    if (req.smart) {
        const auto script = project_script(configuration_.smart_script, "smart_train.py");
        const auto registry = default_model_registry();
        args = {script, "--engine", engine, "--root", root, "--dataset", dataset,
                "--registry", registry, "--name", req.name,
                "--device", req.device, "--gpu", std::to_string(req.gpu),
                "--epochs", std::to_string(req.epochs), "--steps", std::to_string(req.steps),
                "--batch", std::to_string(req.batch_size), "--workers", std::to_string(req.workers),
                "--save-every", std::to_string(req.save_every),
                "--index", req.build_index ? "on" : "off"};
        if (req.offline) args.emplace_back("--offline");
    } else {
        if (seed || req.batch_size < 1 || req.workers < 1 ||
            req.gpu < 0 || req.save_every > req.epochs || req.device == "auto")
            throw std::invalid_argument("Legacy RVC training needs explicit batch/workers/device/gpu");
        const auto script = project_script(configuration_.bridge_script, "rvc_train.py");
        args = {script, "--rvc-root", root, "--dataset", dataset, "--name", req.name,
                "--epochs", std::to_string(req.epochs), "--batch", std::to_string(req.batch_size),
                "--save-every", std::to_string(req.save_every), "--workers", std::to_string(req.workers),
                "--device", req.device, "--gpu", std::to_string(req.gpu),
                "--index", req.build_index ? "on" : "off"};
    }
    int exit_code = -1;
    try {
        exit_code = detail::run_process(
            absolute_executable(resolve_runtime_python(configuration_.python, engine)),
            args, root, configuration_.timeout, logfile);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Training execution error: ") + ex.what() +
                                 "\n" + read_tail(logfile));
    }
    if (exit_code != 0)
        throw std::runtime_error("Training exited with " + std::to_string(exit_code) +
                                 "\n" + read_tail(logfile));
    TrainingResult result;
    result.experiment_dir = exp;
    result.log_file = logfile;
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    if (req.smart) {
        const auto index = default_model_registry();
        for (const auto& model : read_model_registry(index, engine)) {
            if (model.name != req.name) continue;
            if (!fs::is_regular_file(model.primary))
                throw std::runtime_error("Registered training model missing: " + model.primary.string());
            result.primary_model = model.primary;
            result.secondary_model = model.secondary;
            break;
        }
        if (result.primary_model.empty())
            throw std::runtime_error("Training completed but model was not registered; check log: " +
                                     logfile.string());
    }
    return result;
}
}  // namespace myvoice
