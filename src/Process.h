#pragma once
#include <chrono>
#include <filesystem>
#include <vector>

namespace myvoice::detail {

// argv 参数逐一传入，不经过 shell，避免路径中的空格/特殊字符被解释。
int run_process(const std::filesystem::path& executable,
                const std::vector<std::filesystem::path>& args,
                const std::filesystem::path& working_directory,
                std::chrono::milliseconds timeout);

}  // namespace myvoice::detail
