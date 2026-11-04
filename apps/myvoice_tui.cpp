#include "myvoice/CommandShell.h"
#include "myvoice/VoiceConverter.h"
#include "myvoice/VoiceTrainer.h"

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

int run_tui() {
    using namespace ftxui;
    myvoice::CommandShell shell;
    auto app = App::TerminalOutput();
    std::string line = "/";
    std::vector<std::string> history;
    std::vector<std::string> log = {"myVoice 主菜单  |  输入 /Seed-VC 或 /RVC 进入对应引擎"};
    std::size_t history_pos = 0;
    std::mutex worker_mutex;
    std::string worker_message;
    std::atomic<bool> busy{false};
    std::string current_job;
    std::thread worker;

    const auto log_message = [&](const std::string& message) {
        std::istringstream stream(message);
        std::string item;
        while (std::getline(stream, item)) log.push_back(std::move(item));
        if (log.size() > 150) log.erase(log.begin(), log.begin() + 50);
    };
    const auto submit = [&] {
        if (line.empty()) return;
        const std::string command = line;
        history.push_back(command);
        history_pos = history.size();
        line = "/";
        log_message("> " + command);
        const auto result = shell.execute(command);
        if (!result.success) {
            log_message("ERROR: " + result.message);
            return;
        }
        if (result.action == myvoice::CommandAction::Quit) {
            if (busy.load()) log_message("任务正在运行；完成后再退出，或 Ctrl+C 结束程序");
            else app.Exit();
            return;
        }
        if (result.action == myvoice::CommandAction::Convert || result.action == myvoice::CommandAction::Train) {
            if (busy.exchange(true)) {
                log_message("已有转换/训练任务正在运行，请等待完成");
                return;
            }
            if (worker.joinable()) worker.join();  // 之前任务已经结束
            auto config = shell.conversion_configuration();
            config.capture_child_output = true;  // Python stdout/stderr 不得破坏 FTXUI 画面
            const auto request = shell.conversion_request();
            const bool is_training = result.action == myvoice::CommandAction::Train;
            const auto training = shell.training;
            const auto training_config = shell.training_configuration;
            current_job = is_training ? "TRAINING" : "CONVERTING";
            log_message(is_training ? "正在后台训练 RVC：" + training.name +
                " （进度日志在 RVC/logs/<name>/myvoice_train.log）" :
                "正在后台运行 " + shell.menu_name() + "，请查看状态栏...");
            worker = std::thread([&, config, request, is_training, training, training_config] {
                std::string outcome;
                try {
                    if (is_training) {
                        const auto result = myvoice::VoiceTrainer(training_config).train(training);
#ifdef _WIN32
                        outcome = "训练完成: " + result.experiment_dir.u8string();
#else
                        outcome = "训练完成: " + result.experiment_dir.string();
#endif
                    } else {
                        const auto result = myvoice::VoiceConverter(config).convert(request);
#ifdef _WIN32
                        outcome = "转换完成: " + result.output.u8string();
#else
                        outcome = "转换完成: " + result.output.string();
#endif
                        outcome += " (" + std::to_string(result.elapsed.count()) + "ms)";
                    }
                } catch (const std::exception& ex) {
                    outcome = std::string("ERROR: ") + ex.what();
                }
                {
                    std::lock_guard<std::mutex> guard(worker_mutex);
                    worker_message = std::move(outcome);
                }
                busy = false;
                app.PostEvent(Event::Custom);  // Worker 不直接操作 FTXUI 控件
            });
            return;
        }
        if (!result.message.empty()) log_message(result.message);
    };

    InputOption option;
    option.on_enter = submit;
    auto input = Input(&line, "输入 /help", option);
    // 捕获 Tab 执行实际补全；方向键读取命令历史。
    input |= CatchEvent([&](Event event) {
        if (event == Event::Tab) {
            const auto choices = shell.suggestions(line);
            if (!choices.empty()) line = choices.front().replacement;
            return true;
        }
        if (event == Event::ArrowUp && !history.empty()) {
            if (history_pos > 0) --history_pos;
            line = history[history_pos];
            return true;
        }
        if (event == Event::ArrowDown && !history.empty()) {
            if (history_pos + 1 < history.size()) line = history[++history_pos];
            else { history_pos = history.size(); line = "/"; }
            return true;
        }
        return false;
    });
    auto view = Renderer(input, [&] {
        {
            std::lock_guard<std::mutex> guard(worker_mutex);
            if (!worker_message.empty()) {
                log_message(worker_message);
                worker_message.clear();
            }
        }
        std::vector<Element> lines;
        const std::size_t start = log.size() > 10 ? log.size() - 10 : 0;
        for (std::size_t i = start; i < log.size(); ++i)
            lines.push_back(text(log[i]));
        auto hints = shell.suggestions(line);
        std::vector<Element> completion;
        for (std::size_t i = 0; i < std::min<std::size_t>(hints.size(), 6); ++i)
            completion.push_back(text(hints[i].display) | dim);
        if (completion.empty()) completion.push_back(text("输入 / 选择命令；Tab 补全命令、路径与枚举参数") | dim);
        return vbox({
            text(" myVoice / " + shell.menu_name() + "  |  Voice Conversion Console ") | bold | color(Color::Cyan),
            separator(),
            text(busy.load() ? "STATUS: " + current_job + "..." : "STATUS: READY") |
                color(busy.load() ? Color::Yellow : Color::Green),
            separator(),
            vbox(std::move(lines)) | flex,
            separator(),
            hbox({text(" > "), input->Render()}) | border,
            vbox(std::move(completion)),
            text(shell.menu() == myvoice::ConsoleMenu::Main ?
                "Enter 执行 | Tab 补全 | /Seed-VC 进入 Seed-VC | /RVC 进入 RVC | /quit 退出" :
                "Enter 执行 | Tab 补全 | 上下键历史 | /convert 转换 | /back 主菜单 | /help 命令") | dim
        }) | border;
    });
    app.Loop(view);
    if (worker.joinable()) worker.join();
    return 0;
}
