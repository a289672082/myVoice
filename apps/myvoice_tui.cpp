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
    // 首屏展示中文操作引导；命令仍以 / 开头，避免破坏既有用户习惯。
    std::vector<std::string> log = {"欢迎使用 myVoice 中文语音控制台",
        "请输入 /Seed-VC 或 /RVC 进入对应子菜单；输入 /help 查看中文说明"};
    std::size_t history_pos = 0;
    std::mutex worker_mutex;
    std::string worker_message;
    std::atomic<bool> busy{false};
    std::string current_job;
    std::thread worker;

    // 日志统一保留换行，防止 Python 多行信息覆盖当前 FTXUI 控件。
    const auto log_message = [&](const std::string& message) {
        std::istringstream stream(message);
        std::string item;
        while (std::getline(stream, item)) log.push_back(std::move(item));
        if (log.size() > 150) log.erase(log.begin(), log.begin() + 50);
    };
    const auto submit = [&] {
        if (line.empty() && !shell.wizard_active()) return;
        const std::string command = line;
        history.push_back(command);
        history_pos = history.size();
        log_message("> " + (command.empty() ? "（使用默认值）" : command));
        const auto result = shell.execute(command);
        line = shell.wizard_active() ? "" : "/";
        if (!result.success) {
            log_message("错误：" + result.message);
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
            const auto training = shell.active_training();
            const auto training_config = shell.active_training_configuration();
            const auto engine_name = shell.menu_name();
            current_job = is_training ? "训练中" : "转换中";
            log_message(is_training ? "正在后台训练 " + engine_name + "：" + training.name +
                " （输入 /train-log 查看日志，/quality-report 查看质检）" :
                "正在后台运行 " + shell.menu_name() + "，请查看状态栏...");
            worker = std::thread([&, config, request, is_training, training, training_config] {
                std::string outcome;
                try {
                    if (is_training) {
                        const auto result = myvoice::VoiceTrainer(training_config).train(training);
#ifdef _WIN32
                        outcome = "训练完成: " + result.experiment_dir.u8string();
                        if (!result.primary_model.empty())
                            outcome += " | 已注册: " + result.primary_model.u8string();
#else
                        outcome = "训练完成: " + result.experiment_dir.string();
                        if (!result.primary_model.empty())
                            outcome += " | 已注册: " + result.primary_model.string();
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
                    outcome = std::string("错误：") + ex.what();
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

    // 回车执行已输入的 /命令；Tab 补全与历史命令在下方单独处理。
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
        // 候选项来自 CommandShell：保留命令英文原文，右侧附加中文用法说明。
        auto hints = shell.suggestions(line);
        std::vector<Element> completion;
        for (std::size_t i = 0; i < std::min<std::size_t>(hints.size(), 6); ++i)
            completion.push_back(text(hints[i].display) | dim);
        if (completion.empty()) completion.push_back(text("输入 / 选择命令；Tab 补全命令、路径与枚举参数") | dim);
        return vbox({
            (shell.wizard_active() ? text(shell.wizard_hint()) | color(Color::Yellow) : text("")),
            text(" myVoice / " + shell.menu_name() + "  |  中文语音转换控制台 ") | bold | color(Color::Cyan),
            separator(),
            text(busy.load() ? "状态：" + current_job + "..." : "状态：就绪") |
                color(busy.load() ? Color::Yellow : Color::Green),
            separator(),
            vbox(std::move(lines)) | flex,
            separator(),
            hbox({text(" > "), input->Render()}) | border,
            vbox(std::move(completion)),
            text(shell.menu() == myvoice::ConsoleMenu::Main ?
                "回车执行 | Tab 补全 | ↑↓ 历史命令 | /Seed-VC 声音转换 | /RVC 模型训练 | /quit 退出" :
                (shell.wizard_active() ?
                 "新手向导：按提示输入内容并回车，/cancel 取消；没有 / 前缀" :
                 "回车执行 | Tab 补全 | /train-wizard 新手向导 | /train 智能训练 | /models 模型列表 | /back 返回")) | dim
        }) | border;
    });
    app.Loop(view);
    if (worker.joinable()) worker.join();
    return 0;
}
