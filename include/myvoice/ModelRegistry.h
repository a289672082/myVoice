#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace myvoice {
struct RegisteredModel {
    std::string engine;
    std::string name;
    std::filesystem::path primary;
    std::filesystem::path secondary;
};

// 训练助手持久化索引（TSV，4 列），只接受经过验证的引擎及安全的实验名。
// 不解析外部任意 JSON，模型权重始终由对应官方推理引擎加载。
inline std::vector<RegisteredModel> read_model_registry(
    const std::filesystem::path& index,
    const std::string& selected_engine = {}) {
    std::vector<RegisteredModel> models;
    std::ifstream file(index, std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> columns;
        std::size_t first = 0;
        for (std::size_t i = 0; i < 3; ++i) {
            const auto end = line.find('\t', first);
            if (end == std::string::npos) break;
            columns.push_back(line.substr(first, end - first));
            first = end + 1;
        }
        if (columns.size() != 3 || line.find('\t', first) != std::string::npos) continue;
        columns.push_back(line.substr(first));
        if (columns[0] != "rvc" && columns[0] != "seed-vc") continue;
        if (!selected_engine.empty() && columns[0] != selected_engine) continue;
        if (columns[1].empty() || columns[2].empty()) continue;
        models.push_back({columns[0], columns[1],
                          std::filesystem::u8path(columns[2]),
                          std::filesystem::u8path(columns[3])});
    }
    std::sort(models.begin(), models.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    return models;
}
inline std::filesystem::path default_model_registry() {
    namespace fs = std::filesystem;
#ifdef MYVOICE_SOURCE_ROOT
    return fs::u8path(MYVOICE_SOURCE_ROOT) / "models" / "index.tsv";
#else
    fs::path current = fs::current_path();
    for (int i = 0; i < 16; ++i) {
        if (fs::is_regular_file(current / "scripts" / "smart_train.py"))
            return current / "models" / "index.tsv";
        if (current == current.parent_path()) break;
        current = current.parent_path();
    }
    throw std::runtime_error("Cannot locate myVoice models directory; run in repository root");
#endif
}
}  // namespace myvoice
