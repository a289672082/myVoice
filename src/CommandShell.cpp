#include "myvoice/CommandShell.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace myvoice {
namespace {
namespace fs = std::filesystem;
struct CommandInfo { const char* name; const char* usage; const char* description; };
const CommandInfo commands[] = {
    {"/help", "/help", "查看帮助"},
    {"/show", "/show", "当前配置"},
    {"/source", "/source <音频文件>", "原始声音"},
    {"/target", "/target <参考音频>", "目标声音"},
    {"/output", "/output <输出.wav>", "输出位置"},
    {"/seed-root", "/seed-root <目录>", "Seed-VC 所在目录"},
    {"/python", "/python <可执行文件>", "Python 解释器"},
    {"/engine", "/engine speech|singing|rvc", "零样本讲话/歌声或训练模型"},
    {"/model", "/model <模型.pth>", "RVC 已训练音色模型"},
    {"/steps", "/steps <1..200>", "推理步数"},
    {"/timeout", "/timeout <1..86400>", "超时秒数"},
    {"/overwrite", "/overwrite on|off", "覆盖已有结果"},
    {"/rvc-root", "/rvc-root <目录>", "RVC 源码及模型目录"},
    {"/dataset", "/dataset <目录>", "训练数据目录"},
    {"/train-name", "/train-name <名称>", "训练实验名"},
    {"/epochs", "/epochs <1..10000>", "训练轮数"},
    {"/batch", "/batch <1..128>", "训练批次大小"},
    {"/save-every", "/save-every <1..10000>", "保存间隔 epoch"},
    {"/workers", "/workers <1..64>", "训练预处理线程数"},
    {"/device", "/device cuda|cpu", "训练计算设备"},
    {"/gpu", "/gpu <0..31>", "CUDA GPU 编号"},
    {"/index", "/index on|off", "训练结束构建特征索引"},
    {"/train-timeout", "/train-timeout <秒>", "训练子进程超时"},
    {"/train-script", "/train-script <py路径>", "训练桥接脚本，可选"},
    {"/train", "/train", "训练目标声音模型(RVC v2)"},
    {"/train-log", "/train-log", "显示训练日志末尾"},
    {"/convert", "/convert", "开始转换"},
    {"/quit", "/quit", "退出"}
};
bool starts(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \n\r\t");
    return b == std::string::npos ? "" : s.substr(b, s.find_last_not_of(" \n\r\t") - b + 1);
}
// 引号用于包含空格的文件路径，反斜杠原样保留（Windows 路径）。
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> parts;
    std::string word;
    char quote = 0;
    bool active = false;
    for (char c : line) {
        if (quote) { if (c == quote) quote = 0; else word += c; }
        else if (c == '"' || c == '\'') { quote = c; active = true; }
        else if (std::isspace(static_cast<unsigned char>(c))) {
            if (active) { parts.push_back(word); word.clear(); active = false; }
        } else { word += c; active = true; }
    }
    if (quote) throw std::invalid_argument("Unclosed quote");
    if (active) parts.push_back(word);
    return parts;
}
int number(const std::string& value, int low, int high) {
    std::size_t used = 0;
    if (value.empty()) throw std::invalid_argument("Missing number");
    const int n = std::stoi(value, &used);
    if (used != value.size() || n < low || n > high) throw std::invalid_argument("Number out of range: " + value);
    return n;
}
fs::path as_path(const std::string& s) {
    if (s.empty()) throw std::invalid_argument("Path is empty");
#ifdef _WIN32
    return fs::u8path(s);
#else
    return fs::path(s);
#endif
}
std::string as_string(const fs::path& p) {
#ifdef _WIN32
    return p.u8string();
#else
    return p.string();
#endif
}
std::string replacement(const std::string& key, const std::string& path) {
    return key + " " + (path.find_first_of(" \t") == std::string::npos ? path : '"' + path + '"');
}
bool is_path(const std::string& c) {
    return c == "/source" || c == "/target" || c == "/output" || c == "/seed-root" || c == "/python" ||
           c == "/rvc-root" || c == "/dataset" || c == "/train-script" || c == "/model";
}
}  // namespace

CommandResult CommandShell::set_option(const std::string& key, const std::string& value) {
    try {
        if (key == "/rvc-root") { training_configuration.rvc_root = as_path(value); configuration.rvc_root = as_path(value); }
        else if (key == "/model") request.trained_model = as_path(value);
        else if (key == "/dataset") training.dataset = as_path(value);
        else if (key == "/train-script") training_configuration.bridge_script = as_path(value);
        else if (key == "/train-name") {
            if (value.empty()) throw std::invalid_argument("Training name must not be empty");
            training.name = value;
        }
        else if (key == "/epochs") training.epochs = number(value, 1, 10000);
        else if (key == "/batch") training.batch_size = number(value, 1, 128);
        else if (key == "/save-every") training.save_every = number(value, 1, 10000);
        else if (key == "/workers") training.workers = number(value, 1, 64);
        else if (key == "/gpu") training.gpu = number(value, 0, 31);
        else if (key == "/train-timeout") training_configuration.timeout = std::chrono::seconds(number(value, 1, 86400));
        else if (key == "/device") {
            if (value != "cuda" && value != "cpu") throw std::invalid_argument("/device expects cuda or cpu");
            training.device = value;
        }
        else if (key == "/index") {
            if (value != "on" && value != "off") throw std::invalid_argument("/index expects on or off");
            training.build_index = value == "on";
        }
        else if (key == "/source") request.source = as_path(value);
        else if (key == "/target") request.target = as_path(value);
        else if (key == "/output") request.output = as_path(value);
        else if (key == "/seed-root") configuration.seed_vc_root = as_path(value);
        else if (key == "/python") { configuration.python = as_path(value); training_configuration.python = as_path(value); }
        else if (key == "/engine") {
            if (value == "speech") request.mode = Mode::Speech;
            else if (value == "singing") request.mode = Mode::Singing;
            else if (value == "rvc") request.mode = Mode::Rvc;
            else throw std::invalid_argument("/engine expects speech, singing or rvc");
        } else if (key == "/steps") request.diffusion_steps = number(value, 1, 200);
        else if (key == "/timeout") configuration.timeout = std::chrono::seconds(number(value, 1, 86400));
        else if (key == "/overwrite") {
            if (value == "on") request.overwrite = true;
            else if (value == "off") request.overwrite = false;
            else throw std::invalid_argument("/overwrite expects on or off");
        } else throw std::invalid_argument("Unknown command: " + key + " (try /help)");
        return {CommandAction::None, key + " = " + value, true};
    } catch (const std::exception& e) { return {CommandAction::None, e.what(), false}; }
}
CommandResult CommandShell::execute(const std::string& line) {
    try {
        const auto args = tokenize(trim(line));
        if (args.empty()) return {};
        const auto& key = args.front();
        if (key == "/help" || key == "/show" || key == "/convert" || key == "/train" || key == "/train-log" || key == "/quit") {
            if (args.size() != 1) throw std::invalid_argument(key + " takes no arguments");
            if (key == "/help") return {CommandAction::None, help(), true};
            if (key == "/show") return {CommandAction::None, summary(), true};
            if (key == "/train-log") {
                const fs::path log_path = training_configuration.rvc_root / "logs" /
                                          training.name / "myvoice_train.log";
                std::ifstream input(log_path, std::ios::binary);
                if (!input) return {CommandAction::None, "No training log yet: " + as_string(log_path), false};
                input.seekg(0, std::ios::end);
                const std::streamoff end = input.tellg();
                if (end <= 0) return {CommandAction::None, "Training log is empty", true};
                input.seekg(std::max<std::streamoff>(0, end - 1600));
                std::string tail{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
                return {CommandAction::None, tail, true};
            }
            if (key == "/quit") return {CommandAction::Quit, "Goodbye", true};
            if (key == "/train") return {CommandAction::Train, "Training RVC model...", true};
            return {CommandAction::Convert, "Converting...", true};
        }
        if (key[0] != '/') throw std::invalid_argument("Commands must start with / (try /help)");
        if (args.size() != 2) throw std::invalid_argument("Expected one value; quote paths containing spaces");
        return set_option(key, args[1]);
    } catch (const std::exception& e) { return {CommandAction::None, e.what(), false}; }
}
std::string CommandShell::help() {
    std::ostringstream o;
    o << "Slash commands: (Tab autocomplete, Enter execute)\n";
    for (const auto& c : commands) o << "  " << c.usage << "   " << c.description << '\n';
    o << "RVC training: /rvc-root, /dataset, /train-name, /device, /train.\n";
    o << "Quote paths with spaces, e.g. /source \"D:\\audio files\\voice.wav\"\n";
    return o.str();
}
std::string CommandShell::summary() const {
    std::ostringstream o;
    o << "Mode: " << (request.mode == Mode::Speech ? "speech" : request.mode == Mode::Singing ? "singing" : "rvc")
      << "\nSource: " << as_string(request.source)
      << "\nTarget: " << as_string(request.target)
      << "\nOutput: " << as_string(request.output)
      << "\nTrained model: " << as_string(request.trained_model)
      << "\nSeed-VC: " << as_string(configuration.seed_vc_root)
      << "\nPython: " << as_string(configuration.python)
      << "\nSteps: " << request.diffusion_steps
      << "\nTimeout: " << configuration.timeout.count() / 1000 << "s"
      << "\nOverwrite: " << (request.overwrite ? "on" : "off")
      << "\nRVC root: " << as_string(training_configuration.rvc_root)
      << "\nDataset: " << as_string(training.dataset)
      << "\nTrain model: " << training.name
      << "\nEpochs: " << training.epochs << " | Batch: " << training.batch_size
      << " | Workers: " << training.workers
      << "\nDevice: " << training.device << " GPU: " << training.gpu
      << " | Index: " << (training.build_index ? "on" : "off");
    return o.str();
}
std::vector<Suggestion> CommandShell::suggestions(const std::string& line) const {
    std::vector<Suggestion> result;
    const auto blank = line.find_first_of(" \t");
    if (blank == std::string::npos) {
        for (const auto& c : commands) if (starts(c.name, line))
            result.push_back({c.name, std::string(c.usage) + "  -  " + c.description});
        return result;
    }
    const auto key = line.substr(0, blank);
    std::string prefix = trim(line.substr(blank + 1));
    if (!prefix.empty() && (prefix[0] == '"' || prefix[0] == '\'')) prefix.erase(0, 1);
    if (key == "/engine" || key == "/overwrite" || key == "/device" || key == "/index") {
        const std::vector<std::string> choices = key == "/engine" ?
            std::vector<std::string>{"speech", "singing", "rvc"} : key == "/device" ?
            std::vector<std::string>{"cuda", "cpu"} : std::vector<std::string>{"on", "off"};
        for (const auto& c : choices) if (starts(c, prefix))
            result.push_back({replacement(key, c), c});
    } else if (is_path(key)) {
        try {
            fs::path input = prefix.empty() ? fs::path(".") : as_path(prefix);
            fs::path dir = input.has_parent_path() ? input.parent_path() : fs::path(".");
            std::string stem = prefix.empty() ? "" : as_string(input.filename());
            for (const auto& item : fs::directory_iterator(dir)) {
                const auto name = as_string(item.path().filename());
                if (!starts(name, stem)) continue;
                std::string path = as_string(item.path());
                if (item.is_directory()) path += "/";
                result.push_back({replacement(key, path), path});
                if (result.size() >= 16) break;
            }
            std::sort(result.begin(), result.end(), [](const Suggestion& a, const Suggestion& b) {
                return a.replacement < b.replacement;
            });
        } catch (const fs::filesystem_error&) { /* 无目录权限或目录不存在，补全静默失败 */ }
     } else if (key == "/steps" || key == "/timeout" || key == "/epochs" || key == "/batch" || key == "/save-every" || key == "/workers") {
        const std::vector<std::string> choices = key == "/steps" ?
            std::vector<std::string>{"30", "40", "60"} : key == "/epochs" ?
            std::vector<std::string>{"100", "200", "300"} : key == "/batch" ?
            std::vector<std::string>{"2", "4", "8"} : key == "/save-every" ?
            std::vector<std::string>{"10", "20", "50"} : key == "/workers" ?
            std::vector<std::string>{"2", "4", "8"} : std::vector<std::string>{"300", "900", "1800"};
        for (const auto& c : choices) if (starts(c, prefix)) result.push_back({replacement(key, c), c});
    }
    return result;
}
}  // namespace myvoice
