#pragma once
#include "myvoice/VoiceConverter.h"
#include <string>
#include <vector>

namespace myvoice {
enum class CommandAction { None, Convert, Quit };
struct CommandResult {
    CommandAction action = CommandAction::None;
    std::string message;
    bool success = true;
};
struct Suggestion {
    std::string replacement;
    std::string display;
};
// 统一命令模型：FTXUI 与批处理模式均使用 / 命令。
class CommandShell {
public:
    Configuration configuration;
    Request request;
    CommandResult execute(const std::string& line);
    CommandResult set_option(const std::string& command, const std::string& value);
    std::vector<Suggestion> suggestions(const std::string& line) const;
    std::string summary() const;
    static std::string help();
};
}  // namespace myvoice
