#pragma once
#include "myvoice/VoiceConverter.h"
#include "myvoice/VoiceTrainer.h"
#include <string>
#include <vector>

namespace myvoice {

// 【菜单分层】主菜单仅负责切换引擎；Seed-VC 与 RVC 各有独立配置。
// 【兼容要求】对外命令仍采用 /英文名称，控制台文案、解释与提示使用中文。
enum class ConsoleMenu { Main, SeedVC, Rvc };
// 命令只描述用户意图，实际耗时操作由 FTXUI/CLI 在工作线程或批处理内执行。
enum class CommandAction { None, Convert, Train, PrepareGpu, CheckGpu, Quit };
struct CommandResult {
    CommandAction action = CommandAction::None;
    std::string message;
    bool success = true;
};
// replacement 是实际补全后的命令（如 /device auto），display 是带中文解释的提示。
struct Suggestion {
    std::string replacement;
    std::string display;
};

// 【职责划分】该类不依赖 FTXUI；交互控制台与批处理共用中文帮助、路由校验及补全。
// /Seed-VC → V1 歌声 / V2 讲话与微调；/RVC → RVC 训练与推理，禁止跨引擎混用模型。
class CommandShell {
public:
    CommandShell();

    // RVC 和 Seed-VC 训练参数独立，Seed-VC V2 专属微调支持自动注册和加载。
    TrainingRequest training;
    TrainingConfiguration training_configuration;
    TrainingRequest seed_training;
    TrainingConfiguration seed_training_configuration;
    [[nodiscard]] TrainingRequest active_training() const;
    [[nodiscard]] TrainingConfiguration active_training_configuration() const;

    [[nodiscard]] ConsoleMenu menu() const noexcept { return menu_; }
    [[nodiscard]] std::string menu_name() const;
    [[nodiscard]] const Configuration& conversion_configuration() const;
    [[nodiscard]] const Request& conversion_request() const;

    // 执行 /help、/show、/train 等命令；不需要参数的命令也由此分派。
    CommandResult execute(const std::string& line);
    // 修改 /source、/device 等带一个参数的设置；返回中文结果或错误信息。
    CommandResult set_option(const std::string& key, const std::string& value);
    // 根据当前子菜单生成命令名、文件路径、枚举和已登记模型的中文候选提示。
    [[nodiscard]] std::vector<Suggestion> suggestions(const std::string& line) const;
    // 中文参数概览与当前子菜单的中文使用指南。
    [[nodiscard]] std::string summary() const;
    [[nodiscard]] std::string help() const;
    // 新手向导只在交互式 FTXUI 中使用：步骤输入可不带 /，确认前不修改训练配置。
    [[nodiscard]] bool wizard_active() const noexcept { return wizard_step_ != WizardStep::None; }
    [[nodiscard]] std::string wizard_hint() const;

private:
    enum class WizardStep { None, Dataset, Name, Device, Confirm };
    WizardStep wizard_step_ = WizardStep::None;
    TrainingRequest wizard_request_;
    ConsoleMenu menu_ = ConsoleMenu::Main;
    Configuration seed_configuration_;
    Configuration rvc_configuration_;
    Request seed_request_;
    Request rvc_request_;
};
}  // namespace myvoice
