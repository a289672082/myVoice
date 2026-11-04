#include "myvoice/VoiceConverter.h"
#include "Process.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace myvoice {
namespace fs = std::filesystem;
namespace {

struct TemporaryDirectory {
    fs::path path;
    TemporaryDirectory() {
        static std::atomic<unsigned long long> serial{0};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt != 100; ++attempt) {
            const auto name = std::string("myvoice-") + std::to_string(tick) + "-" +
                              std::to_string(serial.fetch_add(1));
            path = fs::temp_directory_path() / name;
            if (fs::create_directory(path)) return;
        }
        throw std::runtime_error("Unable to create temporary output directory");
    }
    ~TemporaryDirectory() { std::error_code ec; fs::remove_all(path, ec); }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
};

fs::path existing_file(const fs::path& p, const char* name) {
    if (p.empty() || !fs::is_regular_file(p) || fs::file_size(p) == 0)
        throw std::invalid_argument(std::string(name) + " must be a non-empty file: " + p.string());
    return fs::canonical(p);
}

bool is_wav(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    char header[12]{};
    f.read(header, sizeof(header));
    return f.gcount() == 12 && std::string(header, 4) == "RIFF" &&
           std::string(header + 8, 4) == "WAVE";
}

fs::path normalized_executable(fs::path exe) {
    if (exe.empty()) throw std::invalid_argument("Python executable is empty");
    // 'python' / 'python3' 交给 PATH 查找；带目录的相对路径则转换为绝对路径。
    if (exe.has_parent_path()) return fs::absolute(exe).lexically_normal();
    return exe;
}
}  // namespace

VoiceConverter::VoiceConverter(Configuration configuration)
    : configuration_(std::move(configuration)) {}

Result VoiceConverter::convert(const Request& request) const {
    const auto started = std::chrono::steady_clock::now();
    if (configuration_.timeout <= std::chrono::milliseconds::zero())
        throw std::invalid_argument("Timeout must be positive");
    if (request.diffusion_steps < 1 || request.diffusion_steps > 200)
        throw std::invalid_argument("diffusion_steps must be between 1 and 200");
    if (request.output.empty() || request.output.extension() != ".wav")
        throw std::invalid_argument("Output path must end with .wav");

    const fs::path source = existing_file(request.source, "Source audio");
    const fs::path target = existing_file(request.target, "Target audio");
    const fs::path output = fs::absolute(request.output).lexically_normal();
    if (fs::weakly_canonical(output) == source || fs::weakly_canonical(output) == target)
        throw std::invalid_argument("Output cannot overwrite source/target audio");
    if (fs::exists(output) && !request.overwrite)
        throw std::invalid_argument("Output already exists (use /overwrite on to replace)");
    if (!fs::is_directory(output.parent_path()))
        throw std::invalid_argument("Output parent directory does not exist");

    if (configuration_.seed_vc_root.empty() || !fs::is_directory(configuration_.seed_vc_root))
        throw std::invalid_argument("Seed-VC root must be an existing directory");
    const fs::path seed_root = fs::canonical(configuration_.seed_vc_root);
    const fs::path script = request.mode == Mode::Speech ? "inference_v2.py" : "inference.py";
    if (!fs::is_regular_file(seed_root / script))
        throw std::invalid_argument("Missing Seed-VC script: " + (seed_root / script).string());

    const auto python = normalized_executable(configuration_.python);
    TemporaryDirectory temporary;
    std::vector<fs::path> args = {
        script, "--source", source, "--target", target, "--output", temporary.path,
        "--diffusion-steps", std::to_string(request.diffusion_steps)
    };
    if (request.mode == Mode::Singing) {
        // 唱歌要保留原始音符/伴奏调性，不自动升降八度。
        args.insert(args.end(), {"--f0-condition", "True", "--auto-f0-adjust", "False",
                                 "--semi-tone-shift", "0"});
    }
    const fs::path log_file = configuration_.capture_child_output ? temporary.path / "backend.log" : fs::path{};
    const auto error_detail = [&] {
        if (log_file.empty()) return std::string{};
        std::ifstream f(log_file, std::ios::binary);
        if (!f) return std::string{};
        f.seekg(0, std::ios::end);
        const std::streamoff end = f.tellg();
        if (end <= 0) return std::string{};
        f.seekg(std::max<std::streamoff>(0, end - 1800));
        const std::string content{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        return std::string("; Python log tail: ") + content;
    };
    int code;
    try {
        code = detail::run_process(python, args, seed_root, configuration_.timeout, log_file);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string(ex.what()) + error_detail());
    }
    if (code != 0) throw std::runtime_error("Seed-VC inference exited with code " + std::to_string(code) + error_detail());

    fs::path produced;
    for (const auto& entry : fs::directory_iterator(temporary.path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".wav") {
            if (!produced.empty()) throw std::runtime_error("Seed-VC produced multiple WAV files");
            produced = entry.path();
        }
    }
    if (produced.empty() || fs::file_size(produced) < 44 || !is_wav(produced))
        throw std::runtime_error("Seed-VC did not produce a valid WAV output");
    const auto copy_mode = request.overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none;
    if (!fs::copy_file(produced, output, copy_mode))
        throw std::runtime_error("Cannot write output file (already exists?)");
    return {output, std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started)};
}

}  // namespace myvoice
