#include "myvoice/CommandShell.h"
#include "myvoice/EnginePaths.h"
#include "myvoice/ModelRegistry.h"
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
constexpr CommandInfo kMain[] = {
    {"/Seed-VC", "/Seed-VC", "进入 Seed-VC 零样本变声菜单"},
    {"/RVC", "/RVC", "进入 RVC 训练和变声菜单"},
    {"/help", "/help", "查看当前菜单命令"},
    {"/show", "/show", "查看当前菜单状态"},
    {"/quit", "/quit", "退出"},
};
constexpr CommandInfo kSeed[] = {
    {"/help", "/help", "查看 Seed-VC 命令"},
    {"/show", "/show", "显示 Seed-VC 配置"},
    {"/python", "/python <Python路径>", "Seed-VC Python 解释器"},
    {"/seed-root", "/seed-root <目录>", "Seed-VC 所在目录"},
    {"/source", "/source <音频文件>", "原始声音文件"},
    {"/target", "/target <音频文件>", "目标音色参考录音"},
    {"/output", "/output <结果.wav>", "输出文件"},
    {"/engine", "/engine speech|singing", "普通讲话或歌声模型"},
    {"/steps", "/steps <1..200>", "扩散推理步数"},
    {"/timeout", "/timeout <1..86400>", "推理超时（秒）"},
    {"/overwrite", "/overwrite on|off", "覆盖已有结果"},
    {"/convert", "/convert", "使用 Seed-VC 进行文件转换"},
    {"/cfm-model", "/cfm-model <模型.pth>", "Seed-VC V2 CFM 微调权重"},
    {"/ar-model", "/ar-model <模型.pth>", "Seed-VC V2 AR 模型"},
    {"/dataset", "/dataset <目录>", "训练目标音色素材"},
    {"/train-name", "/train-name <名称>", "训练模型名称"},
    {"/train-steps", "/train-steps <1..1000000>", "微调步数"},
    {"/epochs", "/epochs <1..10000>", "最大训练轮数"},
    {"/batch", "/batch auto|<1..128>", "自动设置批次"},
    {"/workers", "/workers auto|<0..64>", "自动设置线程数"},
    {"/device", "/device auto|cuda|cpu", "训练显卡自动检测"},
    {"/gpu", "/gpu auto|<0..31>", "GPU 编号"},
    {"/save-every", "/save-every <1..10000>", "检查点保存间隔"},
    {"/offline", "/offline on|off", "是否禁止下载权重"},
    {"/train-timeout", "/train-timeout <秒>", "训练超时"},
    {"/train", "/train", "一键 Seed-VC V2 CFM 微调"},
    {"/train-log", "/train-log", "查看微调日志"},
    {"/quality-report", "/quality-report", "查看质检报告"},
    {"/models", "/models", "列出本引擎模型"},
    {"/unload-model", "/unload-model", "卸载当前微调模型，恢复默认推理"},
    {"/load-model", "/load-model <名称>", "加载微调模型"},
    {"/back", "/back", "返回主菜单"},
    {"/quit", "/quit", "退出"},
};
constexpr CommandInfo kRvc[] = {
    {"/help", "/help", "查看 RVC 命令"},
    {"/show", "/show", "显示 RVC 配置"},
    {"/python", "/python <Python路径>", "RVC Python 解释器"},
    {"/rvc-root", "/rvc-root <目录>", "RVC 源码目录"},
    {"/source", "/source <音频文件>", "推理源音频文件"},
    {"/output", "/output <结果.wav>", "推理输出文件"},
    {"/model", "/model <模型.pth>", "RVC 已训练模型权重"},
    {"/timeout", "/timeout <1..86400>", "推理超时（秒）"},
    {"/overwrite", "/overwrite on|off", "覆盖已有结果"},
    {"/convert", "/convert", "使用已训练的 RVC 模型转换"},
    {"/dataset", "/dataset <目录>", "训练数据目录"},
    {"/train-name", "/train-name <名称>", "训练实验名"},
    {"/epochs", "/epochs <1..10000>", "训练轮数"},
    {"/batch", "/batch auto|<1..128>", "自动批次"},
    {"/save-every", "/save-every <1..10000>", "权重保存间隔"},
    {"/workers", "/workers auto|<0..64>", "自动线程数"},
    {"/device", "/device auto|cuda|cpu", "自动检测训练 GPU"},
    {"/gpu", "/gpu auto|<0..31>", "自动选择 GPU"},
    {"/index", "/index on|off", "训练后构建 FAISS 索引"},
    {"/train-timeout", "/train-timeout <1..86400>", "训练超时（秒）"},
    {"/train-script", "/train-script <路径>", "自定义 RVC 训练桥接脚本"},
    {"/train", "/train", "一键质检/下载预训练权重/自适应训练"},
    {"/train-log", "/train-log", "读取训练日志尾部"},
    {"/models", "/models", "列出已训练模型"},
    {"/unload-model", "/unload-model", "清除当前 RVC 模型选择"},
    {"/load-model", "/load-model <名称>", "加载已登记模型"},
    {"/quality-report", "/quality-report", "查看音频质检报告"},
    {"/offline", "/offline on|off", "禁止自动下载模型"},
    {"/train-legacy", "/train-legacy", "旧版 RVC 手动训练"},
    {"/back", "/back", "返回主菜单"},
    {"/quit", "/quit", "退出"},
};

struct CommandSet { const CommandInfo* first; const CommandInfo* last; };
CommandSet command_set(ConsoleMenu menu) {
    if (menu == ConsoleMenu::SeedVC) return {std::begin(kSeed), std::end(kSeed)};
    if (menu == ConsoleMenu::Rvc) return {std::begin(kRvc), std::end(kRvc)};
    return {std::begin(kMain), std::end(kMain)};
}
std::string lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool starts(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           lower_ascii(s.substr(0, prefix.size())) == lower_ascii(prefix);
}
std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \n\r\t");
    return b == std::string::npos ? "" : s.substr(b, s.find_last_not_of(" \n\r\t") - b + 1);
}
// 引号包住含空格的文件名；反斜杠保留不做转义（兼容 Windows）。
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
    if (used != value.size() || n < low || n > high)
        throw std::invalid_argument("Number out of range: " + value);
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
           c == "/rvc-root" || c == "/dataset" || c == "/train-script" || c == "/model" ||
           c == "/cfm-model" || c == "/ar-model";
}
bool allowed(ConsoleMenu menu, const std::string& key) {
    const auto set = command_set(menu);
    return std::any_of(set.first, set.last, [&](const CommandInfo& c) { return lower_ascii(c.name) == lower_ascii(key); });
}
std::string menu_label(ConsoleMenu menu) {
    if (menu == ConsoleMenu::SeedVC) return "Seed-VC";
    if (menu == ConsoleMenu::Rvc) return "RVC";
    return "Main";
}
}  // namespace

CommandShell::CommandShell() {
    rvc_request_.mode = Mode::Rvc;
    seed_training.engine = TrainingEngine::SeedVC;
    seed_training.name = "seed_voice";
    seed_training.save_every = 200;
}
TrainingRequest CommandShell::active_training() const {
    if (menu_ == ConsoleMenu::Main) throw std::logic_error("Select an engine");
    return menu_ == ConsoleMenu::SeedVC ? seed_training : training;
}
TrainingConfiguration CommandShell::active_training_configuration() const {
    if (menu_ == ConsoleMenu::Main) throw std::logic_error("Select an engine");
    return menu_ == ConsoleMenu::SeedVC ? seed_training_configuration : training_configuration;
}

std::string CommandShell::menu_name() const { return menu_label(menu_); }
const Configuration& CommandShell::conversion_configuration() const {
    if (menu_ == ConsoleMenu::Main) throw std::logic_error("Select /Seed-VC or /RVC first");
    return menu_ == ConsoleMenu::SeedVC ? seed_configuration_ : rvc_configuration_;
}
const Request& CommandShell::conversion_request() const {
    if (menu_ == ConsoleMenu::Main) throw std::logic_error("Select /Seed-VC or /RVC first");
    return menu_ == ConsoleMenu::SeedVC ? seed_request_ : rvc_request_;
}
CommandResult CommandShell::set_option(const std::string& command, const std::string& value) {
    try {
        const auto key = lower_ascii(command);
        if (!allowed(menu_, key))
            throw std::invalid_argument("Command " + command + " is not available in " + menu_name() + "; try /help");
        if (menu_ == ConsoleMenu::Main) throw std::invalid_argument("Choose /Seed-VC or /RVC first");
        TrainingRequest& tr = menu_ == ConsoleMenu::SeedVC ? seed_training : training;
        TrainingConfiguration& tc = menu_ == ConsoleMenu::SeedVC ?
            seed_training_configuration : training_configuration;
        if (key == "/load-model") {
            for (const auto& model : read_model_registry(default_model_registry(),
                    menu_ == ConsoleMenu::SeedVC ? "seed-vc" : "rvc")) {
                if (model.name != value) continue;
                if (!fs::is_regular_file(model.primary))
                    throw std::invalid_argument("模型权重文件不存在: " + as_string(model.primary));
                if (menu_ == ConsoleMenu::SeedVC) {
                    if (!model.secondary.empty() && !fs::is_regular_file(model.secondary))
                        throw std::invalid_argument("Seed-VC AR 权重缺失: " + as_string(model.secondary));
                    seed_request_.mode = Mode::Speech;
                    seed_request_.seed_cfm_model = model.primary;
                    seed_request_.seed_ar_model = model.secondary;
                } else rvc_request_.trained_model = model.primary;
                return {CommandAction::None, "已加载 " + model.engine + "/" + model.name, true};
            }
            throw std::invalid_argument("未找到已注册模型: " + value + "；请先执行 /models");
        }
        if (key == "/dataset") tr.dataset = as_path(value);
        else if (key == "/train-name") {
            if (value.empty()) throw std::invalid_argument("训练模型名称不能为空");
            tr.name = value;
        } else if (key == "/epochs") tr.epochs = number(value, 1, 10000);
        else if (key == "/train-steps") tr.steps = number(value, 1, 1000000);
        else if (key == "/batch") tr.batch_size = value == "auto" ? 0 : number(value, 1, 128);
        else if (key == "/workers") tr.workers = value == "auto" ? 0 : number(value, 0, 64);
        else if (key == "/save-every") tr.save_every = number(value, 1, 10000);
        else if (key == "/gpu") tr.gpu = value == "auto" ? -1 : number(value, 0, 31);
        else if (key == "/device") {
            if (value != "auto" && value != "cpu" && value != "cuda")
                throw std::invalid_argument("/device expects auto|cpu|cuda");
            tr.device = value;
        } else if (key == "/offline") {
            if (value != "on" && value != "off")
                throw std::invalid_argument("/offline expects on|off");
            tr.offline = value == "on";
        } else if (key == "/train-timeout")
            tc.timeout = std::chrono::seconds(number(value, 1, 86400));
        else if (key == "/python") {
            const fs::path p = as_path(value);
            if (menu_ == ConsoleMenu::SeedVC) {
                seed_configuration_.python = p;
                seed_training_configuration.python = p;
            }
            else { rvc_configuration_.python = p; training_configuration.python = p; }
        } else if (key == "/source") {
            (menu_ == ConsoleMenu::SeedVC ? seed_request_ : rvc_request_).source = as_path(value);
        } else if (key == "/output") {
            (menu_ == ConsoleMenu::SeedVC ? seed_request_ : rvc_request_).output = as_path(value);
        } else if (key == "/timeout") {
            (menu_ == ConsoleMenu::SeedVC ? seed_configuration_ : rvc_configuration_).timeout =
                std::chrono::seconds(number(value, 1, 86400));
        } else if (key == "/overwrite") {
            if (value != "on" && value != "off") throw std::invalid_argument("/overwrite expects on or off");
            (menu_ == ConsoleMenu::SeedVC ? seed_request_ : rvc_request_).overwrite = value == "on";
        } else if (menu_ == ConsoleMenu::SeedVC) {
            if (key == "/seed-root") {
                seed_configuration_.seed_vc_root = as_path(value);
                seed_training_configuration.seed_vc_root = as_path(value);
            } else if (key == "/cfm-model") seed_request_.seed_cfm_model = as_path(value);
            else if (key == "/ar-model") seed_request_.seed_ar_model = as_path(value);
            else if (key == "/target") seed_request_.target = as_path(value);
            else if (key == "/engine") {
                if (value == "speech") seed_request_.mode = Mode::Speech;
                else if (value == "singing") seed_request_.mode = Mode::Singing;
                else throw std::invalid_argument("Seed-VC /engine expects speech or singing");
            } else if (key == "/steps") seed_request_.diffusion_steps = number(value, 1, 200);
            else throw std::invalid_argument("Unknown Seed-VC option: " + key);
        } else {
            if (key == "/rvc-root") {
                rvc_configuration_.rvc_root = as_path(value);
                training_configuration.rvc_root = as_path(value);
            } else if (key == "/model") rvc_request_.trained_model = as_path(value);
            else if (key == "/train-script") training_configuration.bridge_script = as_path(value);
            else if (key == "/index") {
                if (value != "on" && value != "off") throw std::invalid_argument("/index expects on or off");
                training.build_index = value == "on";
            } else throw std::invalid_argument("Unknown RVC option: " + key);
        }
        return {CommandAction::None, key + " = " + value, true};
    } catch (const std::exception& e) {
        return {CommandAction::None, e.what(), false};
    }
}

CommandResult CommandShell::execute(const std::string& line) {
    try {
        const auto args = tokenize(trim(line));
        if (args.empty()) return {};
        const auto& key = args.front();
        if (key.empty() || key[0] != '/') throw std::invalid_argument("Commands must start with / (try /help)");
        const auto normalized = lower_ascii(key);
        if (!allowed(menu_, normalized))
            throw std::invalid_argument(key + " is unavailable in " + menu_name() + "; use /help");
        const bool no_value = normalized == "/seed-vc" || normalized == "/rvc" ||
            normalized == "/back" || normalized == "/help" || normalized == "/show" ||
            normalized == "/convert" || normalized == "/train" || normalized == "/train-legacy" ||
            normalized == "/train-log" || normalized == "/models" ||
            normalized == "/quality-report" || normalized == "/unload-model" ||
            normalized == "/quit";
        if (no_value) {
            if (args.size() != 1) throw std::invalid_argument(key + " takes no arguments");
            if (normalized == "/seed-vc") { menu_ = ConsoleMenu::SeedVC; return {CommandAction::None, "已进入 Seed-VC 菜单。输入 /help 查看专属命令。", true}; }
            if (normalized == "/rvc") { menu_ = ConsoleMenu::Rvc; return {CommandAction::None, "已进入 RVC 菜单。输入 /help 查看专属命令。", true}; }
            if (normalized == "/back") { menu_ = ConsoleMenu::Main; return {CommandAction::None, "已返回主菜单。", true}; }
            if (normalized == "/help") return {CommandAction::None, help(), true};
            if (normalized == "/show") return {CommandAction::None, summary(), true};
            if (normalized == "/quit") return {CommandAction::Quit, "Goodbye", true};
            if (normalized == "/convert") return {CommandAction::Convert, "Converting with " + menu_name() + "...", true};
            if (normalized == "/train" || normalized == "/train-legacy") {
                auto& tr = menu_ == ConsoleMenu::SeedVC ? seed_training : training;
                tr.smart = normalized == "/train";
                return {CommandAction::Train, tr.smart ? "启动智能训练/微调..." :
                        "启动旧版 RVC 手动训练...", true};
            }
            if (normalized == "/unload-model") {
                if (menu_ == ConsoleMenu::SeedVC) {
                    seed_request_.seed_cfm_model.clear();
                    seed_request_.seed_ar_model.clear();
                } else rvc_request_.trained_model.clear();
                return {CommandAction::None, "已卸载当前模型", true};
            }
            if (normalized == "/models") {
                const auto models = read_model_registry(default_model_registry(),
                    menu_ == ConsoleMenu::SeedVC ? "seed-vc" : "rvc");
                if (models.empty())
                    return {CommandAction::None, "暂无已登记模型，请先使用 /train", true};
                std::ostringstream result;
                for (const auto& model : models)
                    result << model.name << "  [" << model.engine << "]  "
                           << as_string(model.primary) << '\n';
                return {CommandAction::None, result.str(), true};
            }
            if (normalized == "/quality-report") {
                const bool seed = menu_ == ConsoleMenu::SeedVC;
                const auto& tc = seed ? seed_training_configuration : training_configuration;
                const auto& tr = seed ? seed_training : training;
                const auto root = resolve_engine_root(
                    seed ? tc.seed_vc_root : tc.rvc_root,
                    seed ? "seed-vc" : "rvc",
                    seed ? "train_v2.py" : "train/train.py");
                return {CommandAction::None, as_string(root /
                    (seed ? "runs" : "logs") / tr.name / "myvoice_quality_report.json"), true};
            }
            if (normalized == "/train-log") {
                const bool seed = menu_ == ConsoleMenu::SeedVC;
                const auto& tc = seed ? seed_training_configuration : training_configuration;
                const auto& tr = seed ? seed_training : training;
                const fs::path log_path = resolve_engine_root(
                    seed ? tc.seed_vc_root : tc.rvc_root,
                    seed ? "seed-vc" : "rvc",
                    seed ? "train_v2.py" : "train/train.py") /
                    (seed ? "runs" : "logs") / tr.name / "myvoice_train.log";
                std::ifstream input(log_path, std::ios::binary);
                if (!input) return {CommandAction::None, "No training log yet: " + as_string(log_path), false};
                input.seekg(0, std::ios::end);
                const std::streamoff end = input.tellg();
                if (end <= 0) return {CommandAction::None, "Training log is empty", true};
                input.seekg(std::max<std::streamoff>(0, end - 1600));
                std::string tail{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
                return {CommandAction::None, tail, true};
            }
        }
        if (args.size() != 2) throw std::invalid_argument("Expected one value; quote paths containing spaces");
        return set_option(key, args[1]);
    } catch (const std::exception& e) { return {CommandAction::None, e.what(), false}; }
}

std::string CommandShell::help() const {
    std::ostringstream out;
    out << "myVoice / " << menu_name() << " | / 命令：Tab 自动补全，Enter 执行\n";
    const auto set = command_set(menu_);
    for (auto it = set.first; it != set.last; ++it)
        out << "  " << it->usage << "   " << it->description << '\n';
    if (menu_ == ConsoleMenu::Main)
        out << "先进入 /Seed-VC 或 /RVC，再执行对应模型的操作。\n";
    else
        out << "输入 /back 返回主菜单；文件路径含空格时用双引号包围。\n";
    if (menu_ == ConsoleMenu::SeedVC)
        out << "Seed-VC V2 专属 CFM 微调：/train，/models，/load-model 名称。\n";
    else if (menu_ == ConsoleMenu::Rvc)
        out << "RVC /train 为一键智能训练；/train-legacy 是旧手动流水线。\n";
    return out.str();
}

std::string CommandShell::summary() const {
    std::ostringstream o;
    o << "Menu: " << menu_name();
    if (menu_ == ConsoleMenu::Main) return o.str() + "\nUse /Seed-VC or /RVC to enter an engine menu.";
    const auto& cfg = conversion_configuration();
    const auto& req = conversion_request();
    o << "\nMode: " << (req.mode == Mode::Speech ? "speech" : req.mode == Mode::Singing ? "singing" : "rvc")
      << "\nSource: " << as_string(req.source)
      << "\nOutput: " << as_string(req.output)
      << "\nPython: " << (cfg.python.empty() ? "[auto: bundled portable runtime]" : as_string(cfg.python))
      << "\nTimeout: " << cfg.timeout.count() / 1000 << "s"
      << "\nOverwrite: " << (req.overwrite ? "on" : "off");
    if (menu_ == ConsoleMenu::SeedVC) {
        o << "\nSeed-VC root: " << (cfg.seed_vc_root.empty() ? "third_party/seed-vc (bundled)" : as_string(cfg.seed_vc_root))
          << "\nTarget reference: " << as_string(req.target)
          << "\nDiffusion steps: " << req.diffusion_steps
           << "\nSeed-VC V2 CFM: " << as_string(req.seed_cfm_model)
           << "\nSeed-VC V2 AR: " << as_string(req.seed_ar_model)
           << "\nTraining dataset: " << as_string(seed_training.dataset)
           << "\nTrain name: " << seed_training.name
           << "\nMax steps: " << seed_training.steps
           << " | Device: " << seed_training.device;
    } else {
        o << "\nRVC root: " << (training_configuration.rvc_root.empty() ? "third_party/rvc (bundled)" : as_string(training_configuration.rvc_root))
          << "\nTrained model: " << as_string(req.trained_model)
          << "\nDataset: " << as_string(training.dataset)
          << "\nTrain name: " << training.name
          << "\nEpochs: " << training.epochs << " | Batch: " << training.batch_size
          << " | Workers: " << training.workers
          << "\nDevice: " << training.device << " GPU: " << training.gpu
          << " | Index: " << (training.build_index ? "on" : "off");
    }
    return o.str();
}

std::vector<Suggestion> CommandShell::suggestions(const std::string& line) const {
    std::vector<Suggestion> result;
    const auto blank = line.find_first_of(" \t");
    if (blank == std::string::npos) {
        const auto set = command_set(menu_);
        for (auto it = set.first; it != set.last; ++it)
            if (starts(it->name, line))
                result.push_back({it->name, std::string(it->usage) + "  -  " + it->description});
        return result;
    }
    const auto key = lower_ascii(line.substr(0, blank));
    if (!allowed(menu_, key)) return {};
    std::string prefix = trim(line.substr(blank + 1));
    if (!prefix.empty() && (prefix[0] == '"' || prefix[0] == '\'')) prefix.erase(0, 1);
    if (key == "/engine" || key == "/overwrite" || key == "/device" ||
        key == "/index" || key == "/offline") {
        const std::vector<std::string> choices = key == "/engine" ?
            std::vector<std::string>{"speech", "singing"} : key == "/device" ?
            std::vector<std::string>{"auto", "cuda", "cpu"} : std::vector<std::string>{"on", "off"};
        for (const auto& c : choices) if (starts(c, prefix)) result.push_back({replacement(key, c), c});
    } else if (key == "/load-model") {
        try {
            for (const auto& model : read_model_registry(default_model_registry(),
                     menu_ == ConsoleMenu::SeedVC ? "seed-vc" : "rvc")) {
                if (starts(model.name, prefix))
                    result.push_back({replacement(key, model.name), model.name});
            }
        } catch (const fs::filesystem_error&) {}
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
    } else if (key == "/steps" || key == "/timeout" || key == "/epochs" || key == "/batch" ||
               key == "/save-every" || key == "/workers" || key == "/train-timeout" ||
                key == "/gpu" || key == "/train-steps") {
        const std::vector<std::string> choices = key == "/steps" ?
            std::vector<std::string>{"30", "40", "60"} : key == "/epochs" ?
            std::vector<std::string>{"100", "200", "300"} : key == "/batch" ?
            std::vector<std::string>{"auto", "1", "2", "4", "8"} : key == "/save-every" ?
            std::vector<std::string>{"10", "20", "50"} : key == "/workers" ?
            std::vector<std::string>{"auto", "2", "4", "8"} : key == "/gpu" ?
            std::vector<std::string>{"auto", "0", "1", "2"} : key == "/train-steps" ?
            std::vector<std::string>{"500", "1000", "2000"} :
            std::vector<std::string>{"300", "900", "1800"};
        for (const auto& c : choices) if (starts(c, prefix)) result.push_back({replacement(key, c), c});
    }
    return result;
}
}  // namespace myvoice
