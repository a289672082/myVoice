#include "myvoice/CommandShell.h"
#include "myvoice/VoiceConverter.h"
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef MYVOICE_WITH_FTXUI
int run_tui();
#endif
namespace {
#ifdef _WIN32
// wmain 收到 UTF-16；转换为 UTF-8 供统一 / 命令解析，路径在 Shell 中恢复为 Unicode。
std::string utf8(const wchar_t* value) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) throw std::runtime_error("Cannot convert command-line argument to UTF-8");
    std::string bytes(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, bytes.data(), len, nullptr, nullptr);
    bytes.pop_back();
    return bytes;
}
#endif
int run(const std::vector<std::string>& args) {
    if (args.empty() || (args.size() == 1 && args.front() == "/interactive")) {
#ifdef MYVOICE_WITH_FTXUI
        return run_tui();
#else
        std::cout << "FTXUI disabled in this build; use /help for batch mode.\n";
        return 2;
#endif
    }
    myvoice::CommandShell shell;
    bool request_conversion = false;
    bool request_training = false;
    myvoice::Configuration convert_configuration;
    myvoice::Request convert_request;
    myvoice::TrainingConfiguration train_configuration;
    myvoice::TrainingRequest train_request;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& key = args[i];
        const bool is_action = key == "/convert" || key == "/train" || key == "/train-legacy";
        if (request_conversion || request_training)
            throw std::invalid_argument("/convert or /train must be the final argument");
        if (key == "/Seed-VC" || key == "/seed-vc" || key == "/RVC" || key == "/rvc" ||
            key == "/back" || key == "/help" || key == "/show" ||
            key == "/convert" || key == "/train" || key == "/train-legacy" ||
            key == "/train-log" || key == "/models" || key == "/quality-report" ||
            key == "/unload-model") {
            const auto result = shell.execute(key);
            if (!result.success) throw std::invalid_argument(result.message);
            if (is_action) {
                if (key == "/convert") {
                    request_conversion = true;
                    convert_configuration = shell.conversion_configuration();
                    convert_request = shell.conversion_request();
                } else {
                    request_training = true;
                    train_configuration = shell.active_training_configuration();
                    train_request = shell.active_training();
                }
            } else if (!result.message.empty()) std::cout << result.message << '\n';
            continue;
        }
        if (key == "/quit" || key == "/interactive")
            throw std::invalid_argument(key + " is only supported in interactive mode");
        if (key.empty() || key.front() != '/')
            throw std::invalid_argument("Expected /option; use /help");
        if (i + 1 >= args.size()) throw std::invalid_argument("Missing value after " + key);
        const auto result = shell.set_option(key, args[++i]);
        if (!result.success) throw std::invalid_argument(result.message);
    }
    if (request_training) {
        const auto result = myvoice::VoiceTrainer(train_configuration).train(train_request);
        std::cout << "Training successful: " << result.experiment_dir.string()
                  << "\nLog: " << result.log_file.string()
                  << "\nRegistered model: " << result.primary_model.string() << "\n";
        return 0;
    }
    if (request_conversion) {
        const auto result = myvoice::VoiceConverter(convert_configuration).convert(convert_request);
        std::cout << "Conversion successful: " << result.output.string() << " ("
                  << result.elapsed.count() << " ms)\n";
    }
    return 0;
}
}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    try {
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i) {
#ifdef _WIN32
            args.push_back(utf8(argv[i]));
#else
            args.emplace_back(argv[i]);
#endif
        }
        return run(args);
    } catch (const std::exception& e) {
        std::cerr << "myVoice error: " << e.what() << '\n';
        return 1;
    }
}
