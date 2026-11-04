#pragma once

#include <chrono>
#include <filesystem>

namespace myvoice {

// 讲话使用 Seed-VC V2；歌声使用 Seed-VC V1 的 F0 条件模型。
enum class Mode { Speech, Singing };

struct Configuration {
#ifdef _WIN32
    std::filesystem::path python = L"python";
#else
    std::filesystem::path python = "python3";
#endif
    std::filesystem::path seed_vc_root;
    std::chrono::milliseconds timeout{900000};
    bool capture_child_output = false;  // 用于 FTXUI: 防止 Python 输出打乱终端界面
};

struct Request {
    std::filesystem::path source;  // 原始语音/歌声
    std::filesystem::path target;  // 目标说话人参考音频，不需要训练
    std::filesystem::path output;  // 最终输出的 WAV 文件
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
    // 推理通过外部 Python 子进程执行；这里不进行 RVC 训练。
    [[nodiscard]] Result convert(const Request& request) const;

private:
    Configuration configuration_;
};

}  // namespace myvoice
