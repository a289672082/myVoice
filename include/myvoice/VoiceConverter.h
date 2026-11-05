#pragma once

#include <chrono>
#include <filesystem>

namespace myvoice {

// 讲话使用 Seed-VC V2；歌声使用 Seed-VC V1 的 F0 条件模型。
enum class Mode { Speech, Singing, Rvc };

struct Configuration {
std::filesystem::path python; // 默认自动定位 runtime/<平台>/<引擎>/ 的便携运行时；/python 可覆盖
    std::filesystem::path seed_vc_root;
    std::filesystem::path rvc_root;
    std::chrono::milliseconds timeout{900000};
    bool capture_child_output = false;  // 用于 FTXUI: 防止 Python 输出打乱终端界面
};

struct Request {
    std::filesystem::path source;  // 原始语音/歌声
    std::filesystem::path target;  // Seed-VC 模式的目标参考音频
    std::filesystem::path output;  // 最终输出的 WAV 文件
    std::filesystem::path trained_model; // RVC 模式的 .pth 文件
    Mode mode = Mode::Speech;
    int diffusion_steps = 30;     // 1..200；高步数通常更慢
    bool overwrite = false;
};

struct Result {
    std::filesystem::path output;
    std::chrono::milliseconds elapsed{};
};

class VoiceConverter {
public:
    explicit VoiceConverter(Configuration configuration);

    // 失败时抛出 std::invalid_argument / std::runtime_error。
    // Seed-VC / RVC 推理通过外部 Python 子进程执行，模型训练由 VoiceTrainer 负责。
    [[nodiscard]] Result convert(const Request& request) const;

private:
    Configuration configuration_;
};

}  // namespace myvoice
