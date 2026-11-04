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
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& key = args[i];
        if (key == "/help" || key == "/show" || key == "/convert" || key == "/train" || key == "/train-log") {
            const auto result = shell.execute(key);
            if (!result.success) throw std::invalid_argument(result.message);
            if (key == "/convert") request_conversion = true;
            else if (key == "/train") request_training = true;
            else std::cout << result.message << '\n';
            continue;
        }
        if (key == "/quit" || key == "/interactive")
            throw std::invalid_argument(key + " is only supported in interactive mode");
        if (key.empty() || key.front() != '/')
            throw std::invalid_argument("Expected /option; use /help");
        if (i + 1 >= args.size()) throw std::invalid_argument("Missing value after " + key);
        const auto result = shell.set_option(key, args[++i]);
        if (!result.success) throw std::invalid_argument(result.message);
        if (key == "/source" || key == "/target" || key == "/output" || key == "/model")
            request_conversion = true;
    }
    if (request_training && request_conversion)
        throw std::invalid_argument("Specify /train or /convert, not both");
    if (request_training) {
        const auto result = myvoice::VoiceTrainer(shell.training_configuration).train(shell.training);
        std::cout << "Training successful: " << result.experiment_dir.string()
                  << "\nLog: " << result.log_file.string() << "\n";
        return 0;
    }
    if (!request_conversion) return 0;
    const auto result = myvoice::VoiceConverter(shell.configuration).convert(shell.request);
    std::cout << "Conversion successful: " << result.output.string() << " ("
              << result.elapsed.count() << " ms)\n";
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
