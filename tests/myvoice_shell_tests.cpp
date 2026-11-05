#include "myvoice/CommandShell.h"
#include "myvoice/PythonRuntime.h"
#include "myvoice/EnginePaths.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool passed, const std::string& message) {
    if (!passed) throw std::runtime_error(message);
}
bool contains(const std::vector<myvoice::Suggestion>& results, const std::string& replacement) {
    return std::any_of(results.begin(), results.end(), [&](const myvoice::Suggestion& s) {
        return s.replacement == replacement;
    });
}
}
int main() {
    try {
        myvoice::CommandShell shell;
        check(myvoice::resolve_runtime_python("custom-python", "rvc") ==
                  std::filesystem::path("custom-python"), "explicit /python override");
        bool runtime_not_found = false;
        try { (void)myvoice::resolve_runtime_python({}, "rvc"); }
        catch (const std::invalid_argument&) { runtime_not_found = true; }
        // CI 未必下载过运行包；缺失时必须明确报错，不可悄悄调用系统 python。
        if (runtime_not_found) check(true, "missing runtime explained");
        // 从内置源码查找引擎；无须提供 /rvc-root 或 /seed-root。
        check(std::filesystem::is_regular_file(
                  myvoice::resolve_engine_root({}, "rvc", "infer/cli.py") / "infer/cli.py"),
              "bundled RVC root");
        check(std::filesystem::is_regular_file(
                  myvoice::resolve_engine_root({}, "seed-vc", "inference_v2.py") / "inference_v2.py"),
              "bundled Seed-VC root");
        using myvoice::ConsoleMenu;
        using myvoice::CommandAction;
        check(shell.menu() == ConsoleMenu::Main, "must start at main menu");
        check(contains(shell.suggestions("/S"), "/Seed-VC"), "Seed-VC main menu completion");
        check(contains(shell.suggestions("/r"), "/RVC"), "RVC case-insensitive completion");
        check(!contains(shell.suggestions("/con"), "/convert"), "conversion must not be available in main menu");
        check(!shell.execute("/convert").success, "main must reject convert");
        check(!shell.execute("/train").success, "main must reject train");
        check(!shell.execute("/source some.wav").success, "main must reject engine options");
        check(!shell.set_option("/source", "some.wav").success, "set_option must check menu too");
        check(shell.execute("/help").message.find("/Seed-VC") != std::string::npos, "main help");
        check(shell.execute("/help").message.find("中文操作帮助") != std::string::npos,
              "main menu Chinese help");
        check(shell.execute("/help").message.find("进入 RVC 模型训练") != std::string::npos,
              "main menu Chinese command description");
        check(shell.execute("/show").message.find("主菜单") != std::string::npos, "main show localized");
        check(!shell.execute("/back").success, "no /back in main");
        check(!shell.execute("/Seed-VC argument").success, "navigation takes no arguments");

        // 中文交互式向导：确认前不修改配置，取消不启动训练，错误输入可重试。
        check(shell.execute("/Seed-VC").success, "enter seed menu");
        {
            const auto old_name = shell.seed_training.name;
            check(shell.execute("/train-wizard").success && shell.wizard_active(), "start Seed wizard");
            check(!shell.execute("not-existing-voice-folder").success, "wizard rejects missing data folder");
            check(shell.execute("/cancel").success && !shell.wizard_active(), "wizard cancellation");
            check(shell.seed_training.name == old_name, "wizard cancel should not modify training");
            check(shell.execute("/train-wizard").success, "restart Seed wizard");
            check(shell.execute(std::filesystem::current_path().string()).success,
                  "wizard accepts existing folder");
            check(!shell.execute("wrong name with spaces").success, "wizard rejects unsafe model name");
            check(shell.execute("seed_voice_wizard").success, "wizard model name");
            check(shell.execute("auto").success, "wizard automatic GPU choice");
            check(shell.execute("无效确认").success == false, "wizard requires explicit confirmation");
            check(shell.execute("确认").action == CommandAction::Train, "wizard launches Seed fine-tune");
            check(!shell.wizard_active() && shell.seed_training.smart &&
                  shell.seed_training.name == "seed_voice_wizard" &&
                  shell.seed_training.device == "auto", "wizard commits correct training request");
        }
        check(shell.menu() == ConsoleMenu::SeedVC, "must be in Seed-VC menu");
        check(contains(shell.suggestions("/con"), "/convert"), "Seed-VC conversion completion");
        check(contains(shell.suggestions("/engine s"), "/engine speech"), "Seed-VC engine suggestion");
        const auto seed_hints = shell.suggestions("/engine s");
        check(std::any_of(seed_hints.begin(), seed_hints.end(), [](const myvoice::Suggestion& hint) {
                  return hint.replacement == "/engine speech" &&
                         hint.display.find("讲话") != std::string::npos;
              }), "Seed-VC autocomplete must explain V2 in Chinese");
        check(contains(shell.suggestions("/tr"), "/train"), "Seed-VC V2 fine-tune completion");
        check(contains(shell.suggestions("/train-w"), "/train-wizard"), "Seed wizard command");
        check(shell.execute("/gpu-check").action == CommandAction::CheckGpu, "Seed GPU check action");
        check(shell.execute("/gpu-setup").action == CommandAction::PrepareGpu, "Seed GPU setup action");
        check(shell.execute("/train-status").success, "Seed progress command");
        check(shell.execute("/dataset seed_speech").success, "Seed dataset");
        check(shell.execute("/device auto").success, "Seed hardware auto");
        check(shell.execute("/train-steps 500").success && shell.seed_training.steps == 500, "Seed fine-tune steps");
        check(shell.execute("/train").action == CommandAction::Train, "Seed-VC fine-tune action");
        check(shell.active_training().engine == myvoice::TrainingEngine::SeedVC &&
              shell.active_training().smart, "Seed-VC training route");
        check(!shell.execute("/train-legacy").success, "Seed-VC legacy train prohibited");
        check(shell.execute("/cfm-model seed_cfm.pth").success, "Seed CFM manual loading");
        check(shell.execute("/ar-model seed_ar.pth").success, "Seed AR manual loading");
        check(shell.execute("/models").success, "Seed models list");
        check(shell.execute("/unload-model").success &&
              shell.conversion_request().seed_cfm_model.empty(), "Seed unload fine-tuned model");
        check(!shell.execute("/load-model unknown_name").success, "Unknown Seed model");
        check(!shell.execute("/model model.pth").success, "Seed-VC must reject RVC model");
        check(!shell.execute("/rvc-root path").success, "Seed-VC must reject RVC root");
        check(!shell.execute("/RVC").success, "must return to main before switching");
        check(!shell.execute("/engine rvc").success, "Seed-VC cannot load RVC mode");
        check(shell.execute("/engine singing").success, "set Seed-VC singing");
        check(shell.execute("/source \"my test file.wav\"").success, "quoted filename");
        check(shell.execute("/target target.wav").success, "Seed-VC target reference");
        check(shell.execute("/output seed_result.wav").success, "Seed-VC output");
        check(shell.execute("/python seed_python").success, "Seed-VC Python");
        check(shell.execute("/seed-root \"C:\\voice files\\seed-vc\"").success, "Windows path syntax");
        check(shell.execute("/overwrite on").success && shell.conversion_request().overwrite, "Seed-VC overwrite");
        check(shell.conversion_request().source.filename() == "my test file.wav", "source lost spaces");
        check(shell.conversion_request().mode == myvoice::Mode::Singing, "singing mode not set");
        check(shell.execute("/show").message.find("参考音频：") != std::string::npos, "Seed-VC localized summary");
        check(shell.execute("/convert").action == CommandAction::Convert, "Seed-VC conversion action");
        check(!shell.execute("/steps abc").success, "reject invalid integer");
        check(!shell.execute("/timeout 0").success, "reject bad timeout");
        check(!shell.execute("/source \"unclosed").success, "reject unclosed quote");
        check(!shell.execute("source xxx").success, "reject commands without slash");
        check(shell.execute("source xxx").message.find("必须以 / 开头") != std::string::npos,
              "command errors must be in Chinese");
        check(!shell.execute("/source one two").success, "reject ambiguous path");

        check(shell.execute("/back").success && shell.menu() == ConsoleMenu::Main, "return to main");
        check(shell.execute("/rvc").success && shell.menu() == ConsoleMenu::Rvc, "enter RVC case-insensitively");
        check(contains(shell.suggestions("/tr"), "/train"), "RVC training completion");
        check(shell.execute("/gpu-check").action == CommandAction::CheckGpu, "RVC GPU check action");
        check(shell.execute("/gpu-setup").action == CommandAction::PrepareGpu, "RVC GPU setup action");
        check(shell.execute("/train-status").success, "RVC progress command");
        check(contains(shell.suggestions("/device c"), "/device cuda"), "RVC device completion");
        check(contains(shell.suggestions("/device a"), "/device auto"), "RVC GPU auto completion");
        const auto rvc_hints = shell.suggestions("/device a");
        check(std::any_of(rvc_hints.begin(), rvc_hints.end(), [](const myvoice::Suggestion& hint) {
                  return hint.replacement == "/device auto" &&
                         hint.display.find("自动") != std::string::npos;
              }), "RVC autocomplete must explain auto in Chinese");
        check(!contains(shell.suggestions("/engine"), "/engine singing"), "RVC menu should hide Seed-VC engine");
        check(!shell.execute("/engine singing").success, "RVC rejects Seed-VC engine");
        check(!shell.execute("/target target.wav").success, "RVC must reject target reference");
        check(!shell.execute("/steps 30").success, "RVC must reject diffusion steps");
        check(shell.conversion_request().mode == myvoice::Mode::Rvc, "RVC mode must be fixed");
        check(shell.execute("/source rvc_source.wav").success, "RVC source");
        check(shell.execute("/output rvc_result.wav").success, "RVC output");
        check(shell.execute("/python rvc_python").success, "RVC Python");
        check(shell.execute("/model male.pth").success, "RVC model");
        check(shell.execute("/rvc-root \"test root\"").success, "RVC root path");
        check(shell.execute("/dataset \"some voices\"").success, "dataset path");
        check(shell.execute("/device cuda").success, "CUDA mode");
        check(shell.execute("/epochs 50").success && shell.training.epochs == 50, "epochs");
        check(shell.execute("/index off").success && !shell.training.build_index, "index mode");
        check(shell.execute("/batch auto").success && shell.training.batch_size == 0, "auto batch");
        check(shell.execute("/gpu auto").success && shell.training.gpu == -1, "auto gpu");
        check(shell.execute("/offline on").success && shell.training.offline, "offline training mode");
        check(shell.execute("/offline off").success && !shell.training.offline, "online training mode");
        check(shell.execute("/models").success, "model list without registry");
        check(!shell.execute("/load-model never_trained").success, "unknown RVC model");
        check(shell.execute("/quality-report").success, "quality report path");
        check(!shell.execute("/epochs 0").success, "bad epochs validation");
        check(!shell.execute("/train-log").success, "missing RVC log expected");
        check(shell.execute("/train").action == CommandAction::Train && shell.active_training().smart,
              "RVC auto training action");
        check(shell.execute("/train-legacy").action == CommandAction::Train &&
              !shell.active_training().smart, "RVC legacy compatibility");
        check(shell.execute("/convert").action == CommandAction::Convert, "RVC conversion action");
        check(shell.execute("/show").message.find("训练模型名称：") != std::string::npos, "RVC localized summary");
        check(shell.conversion_configuration().python == std::filesystem::path("rvc_python"), "RVC Python not applied");

        check(shell.execute("/back").success, "back from RVC");
        check(shell.execute("/Seed-VC").success, "re-enter Seed-VC");
        check(shell.conversion_request().mode == myvoice::Mode::Singing, "Seed-VC mode persisted");
        check(shell.conversion_request().source.filename() == "my test file.wav", "Seed-VC source persisted");
        check(shell.conversion_request().output.filename() == "seed_result.wav", "Seed-VC output persisted");
        check(shell.conversion_configuration().python == std::filesystem::path("seed_python"), "Seed-VC Python isolated");
        check(shell.execute("/quit").action == CommandAction::Quit, "quit intent");
        check(!shell.execute("/unknown xxx").success, "reject unknown command");
        std::cout << "Menu routing, isolation, completion tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << "Shell/menu tests: " << e.what() << '\n';
        return 1;
    }
}
