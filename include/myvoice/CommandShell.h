#pragma once
#include "myvoice/VoiceConverter.h"
#include "myvoice/VoiceTrainer.h"
#include <string>
#include <vector>

namespace myvoice {

// 主菜单只用于导航；两套引擎使用独立的配置和文件路径，避免跨菜单串用模型。
enum class ConsoleMenu { Main, SeedVC, Rvc };
enum class CommandAction { None, Convert, Train, Quit };
struct CommandResult {
    CommandAction action = CommandAction::None;
    std::string message;
    bool success = true;
};
struct Suggestion {
    std::string replacement;
    std::string display;
};

// 控制台路由逻辑不依赖 FTXUI，TUI 与无界面批处理共享菜单权限和自动补全。
class CommandShell {
public:
    CommandShell();

    // 训练目前仅由 RVC 提供。Seed-VC 专属菜单仍然只开放零样本推理。
    TrainingRequest training;
    TrainingConfiguration training_configuration;

    [[nodiscard]] ConsoleMenu menu() const noexcept { return menu_; }
    [[nodiscard]] std::string menu_name() const;
    [[nodiscard]] const Configuration& conversion_configuration() const;
    [[nodiscard]] const Request& conversion_request() const;

    CommandResult execute(const std::string& line);
    CommandResult set_option(const std::string& key, const std::string& value);
    [[nodiscard]] std::vector<Suggestion> suggestions(const std::string& line) const;
    [[nodiscard]] std::string summary() const;
    [[nodiscard]] std::string help() const;

private:
    ConsoleMenu menu_ = ConsoleMenu::Main;
    Configuration seed_configuration_;
    Configuration rvc_configuration_;
    Request seed_request_;
    Request rvc_request_;
};
}  // namespace myvoice
