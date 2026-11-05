#pragma once
#include <chrono>
#include <filesystem>
#include <string>

namespace myvoice {
enum class TrainingEngine { Rvc, SeedVC };

// 保留原始 RVC 手动训练兼容性；smart=true 会执行自动质检、下载与硬件检测。
struct TrainingRequest {
    std::filesystem::path dataset;
    std::string name = "myvoice";
    TrainingEngine engine = TrainingEngine::Rvc;
    bool smart = false;
    bool offline = false;       // 不自动下载缺失的预训练文件
    int epochs = 200;
    int steps = 1000;           // Seed-VC V2 微调的最大步数
    int batch_size = 0;         // 智能模式：0=自动
    int save_every = 20;
    int workers = 0;            // 智能模式：0=自动
    std::string device = "auto"; // auto / cuda / cpu
    int gpu = -1;               // -1=自动选择可用显卡
    bool build_index = true;
};
struct TrainingConfiguration {
    std::filesystem::path python;
    std::filesystem::path rvc_root;
    std::filesystem::path seed_vc_root;
    std::filesystem::path bridge_script; // 手动 RVC 训练桥接脚本
    std::filesystem::path smart_script;  // 智能训练器，默认为 scripts/smart_train.py
    std::chrono::milliseconds timeout{std::chrono::hours(12)};
};
struct TrainingResult {
    std::filesystem::path experiment_dir;
    std::filesystem::path log_file;
    std::chrono::milliseconds elapsed{};
    std::filesystem::path primary_model;   // 训练成功自动注册模型；RVC pth / Seed-VC CFM pth
    std::filesystem::path secondary_model; // Seed-VC AR 检查点
};
class VoiceTrainer {
public:
    explicit VoiceTrainer(TrainingConfiguration configuration);
    [[nodiscard]] TrainingResult train(const TrainingRequest& request) const;
    // 复制便携 CPU 环境到独立 -gpu 目录，验证 CUDA 后才启用；dry_run 不下载。
    [[nodiscard]] std::filesystem::path prepare_gpu(TrainingEngine engine, bool dry_run = false) const;
private:
    TrainingConfiguration configuration_;
};
} // namespace myvoice
