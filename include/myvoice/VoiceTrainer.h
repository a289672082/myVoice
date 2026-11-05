#pragma once
#include <chrono>
#include <filesystem>
#include <string>

namespace myvoice {

// RVC v2 单说话人 F0 音色训练；目标男声录音必须拥有使用许可。
struct TrainingRequest {
    std::filesystem::path dataset; // 训练音频所在目录，支持 WAV/FLAC/MP3 等（由 RVC/ffmpeg 解码）。
    std::string name = "myvoice";  // 安全的实验名，生成到 RVC/logs/<name>。
    int epochs = 200;
    int batch_size = 4;
    int save_every = 20;
    int workers = 4;
    std::string device = "cuda"; // cuda 或 cpu；GPU 优先。
    int gpu = 0;
    bool build_index = true;
};
struct TrainingConfiguration {
std::filesystem::path python; // 默认使用 runtime/<平台>/rvc/ 中的免安装 Python
    std::filesystem::path rvc_root;
    std::filesystem::path bridge_script; // 默认通过 CMake 定位 scripts/rvc_train.py。
    std::chrono::milliseconds timeout{std::chrono::hours(12)};
};
struct TrainingResult {
    std::filesystem::path experiment_dir;
    std::filesystem::path log_file;
    std::chrono::milliseconds elapsed{};
};
class VoiceTrainer {
public:
    explicit VoiceTrainer(TrainingConfiguration configuration);
    [[nodiscard]] TrainingResult train(const TrainingRequest& request) const;
private:
    TrainingConfiguration configuration_;
};
} // namespace myvoice
