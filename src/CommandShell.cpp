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
// 每条命令包含：固定的 /英文命令名、用法示例、面向用户的中文说明。
// 命令名保持不变，以兼容既有脚本和 Tab 自动补全。
struct CommandInfo { const char* name; const char* usage; const char* description; };
// 【主菜单】只允许选择引擎、查看帮助和退出，不提供训练/转换指令。
constexpr CommandInfo kMain[] = {
    {"/Seed-VC", "/Seed-VC", "进入 Seed-VC 语音转换及 V2 微调子菜单"},
    {"/RVC", "/RVC", "进入 RVC 模型训练和语音转换子菜单"},
    {"/help", "/help", "显示当前菜单的中文命令帮助"},
    {"/show", "/show", "查看当前菜单及已设置参数"},
    {"/quit", "/quit", "退出程序"},
};
// 【Seed-VC 子菜单】speech 使用 V2 推理/CFM 微调；singing 使用 V1 歌声模型。
// 两种模式不能混用检查点，Seed-VC 也不能直接加载 RVC 训练权重。
constexpr CommandInfo kSeed[] = {
    // 基本设置：帮助、状态、可选的 Python 解释器与源码目录。
    {"/help", "/help", "查看 Seed-VC 命令"},
    {"/show", "/show", "显示 Seed-VC 配置"},
    {"/python", "/python <Python路径>", "Seed-VC Python 解释器"},
    {"/seed-root", "/seed-root <目录>", "Seed-VC 所在目录"},
    // 语音转换：先设置源音频、参考音频和输出文件，再执行 /convert。
    {"/source", "/source <音频文件>", "设置需要转换的源音频"},
    {"/target", "/target <音频文件>", "设置目标音色参考录音（V2 微调后仍需提供）"},
    {"/output", "/output <结果.wav>", "输出文件"},
    {"/engine", "/engine speech|singing", "选择模式：speech=讲话 V2，singing=歌声 V1"},
    {"/steps", "/steps <1..200>", "设置扩散推理步数（越大通常越慢）"},
    {"/timeout", "/timeout <1..86400>", "推理超时（秒）"},
    {"/overwrite", "/overwrite on|off", "是否允许覆盖已有输出：on=允许，off=禁止"},
    {"/convert", "/convert", "使用 Seed-VC 进行文件转换"},
        // V2 微调模型加载：CFM/AR 只能供 V2 speech 使用，不能用于 V1 singing。
    {"/cfm-model", "/cfm-model <模型.pth>", "手工指定 Seed-VC V2 微调后的 CFM 权重"},
    {"/ar-model", "/ar-model <模型.pth>", "手工指定 Seed-VC V2 的 AR 权重"},
        // 智能微调：指定目标音色素材目录和实验名称；自动检查权重、GPU 和录音质量。
    {"/dataset", "/dataset <目录>", "设置用于训练的目标音色录音目录"},
    {"/train-name", "/train-name <名称>", "训练模型名称"},
    {"/train-steps", "/train-steps <1..1000000>", "指定 Seed-VC V2 微调最大训练步数"},
    {"/epochs", "/epochs <1..10000>", "最大训练轮数"},
    {"/batch", "/batch auto|<1..128>", "训练批大小：auto=按设备显存自动选择"},
    {"/workers", "/workers auto|<0..64>", "数据加载线程数：auto=自动配置"},
    {"/device", "/device auto|cuda|cpu", "训练设备：auto=自动检测，cuda=显卡，cpu=处理器"},
    {"/gpu", "/gpu auto|<0..31>", "指定显卡序号：auto=自动选择"},
    {"/save-every", "/save-every <1..10000>", "检查点保存间隔"},
    {"/offline", "/offline on|off", "权重下载：on=离线不下载，off=允许自动下载"},
    {"/train-timeout", "/train-timeout <秒>", "训练超时"},
    {"/train", "/train", "自动质检录音、准备权重并微调 Seed-VC V2 CFM"},
    {"/train-log", "/train-log", "查看微调日志"},
    {"/quality-report", "/quality-report", "查看音频质检报告的保存路径"},
        // 模型管理：按名称加载已登记模型；Tab 可自动补全已注册的模型名。
    {"/models", "/models", "列出 Seed-VC 已登记的微调模型"},
    {"/unload-model", "/unload-model", "卸载当前微调模型，恢复默认推理"},
    {"/load-model", "/load-model <名称>", "按名称加载已登记的 Seed-VC V2 微调模型"},
    {"/back", "/back", "返回主菜单"},
    {"/quit", "/quit", "退出"},
};
// 【RVC 子菜单】训练和推理使用 RVC 自身的权重；不接受 Seed-VC 检查点。
constexpr CommandInfo kRvc[] = {
    // 基本设置：帮助、状态及可选的本地 Python / 源码路径。
    {"/help", "/help", "查看 RVC 命令"},
    {"/show", "/show", "显示 RVC 配置"},
    {"/python", "/python <Python路径>", "RVC Python 解释器"},
    {"/rvc-root", "/rvc-root <目录>", "RVC 源码目录"},
    // 语音转换：可通过 /model 指定文件，或 /load-model 按名称加载已注册权重。
    {"/source", "/source <音频文件>", "设置需要转换的源音频"},
    {"/output", "/output <结果.wav>", "推理输出文件"},
    {"/model", "/model <模型.pth>", "手工指定已经训练好的 RVC 推理模型 .pth"},
    {"/timeout", "/timeout <1..86400>", "推理超时（秒）"},
    {"/overwrite", "/overwrite on|off", "覆盖已有结果"},
    {"/convert", "/convert", "使用当前选择的 RVC 模型转换源音频"},
    // 智能训练：填写录音目录和实验名，然后 /train；其余参数可保持 auto。
    {"/dataset", "/dataset <目录>", "设置用于训练的目标音色录音目录"},
    {"/train-name", "/train-name <名称>", "训练实验名"},
    {"/epochs", "/epochs <1..10000>", "训练轮数"},
    {"/batch", "/batch auto|<1..128>", "训练批大小：auto=按设备显存自动选择"},
    {"/save-every", "/save-every <1..10000>", "权重保存间隔"},
    {"/workers", "/workers auto|<0..64>", "数据加载线程数：auto=自动配置"},
    {"/device", "/device auto|cuda|cpu", "训练设备：auto=自动检测，cuda=显卡，cpu=处理器"},
    {"/gpu", "/gpu auto|<0..31>", "指定显卡序号：auto=自动选择"},
    {"/index", "/index on|off", "训练后是否生成 FAISS 检索索引"},
    {"/train-timeout", "/train-timeout <1..86400>", "训练超时（秒）"},
    {"/train-script", "/train-script <路径>", "自定义 RVC 训练桥接脚本"},
    {"/train", "/train", "自动检查录音与权重、选择显卡并训练 RVC"},
    {"/train-log", "/train-log", "读取训练日志尾部"},
        // 模型管理：自动读取训练结束后登记的 RVC 模型清单。
    {"/models", "/models", "列出已经登记的 RVC 训练模型"},
    {"/unload-model", "/unload-model", "清除当前 RVC 模型选择"},
    {"/load-model", "/load-model <名称>", "按模型名称加载 RVC 训练权重"},
    {"/quality-report", "/quality-report", "显示音频筛选报告文件路径"},
    {"/offline", "/offline on|off", "权重下载：on=离线不下载，off=允许自动下载"},
    {"/train-legacy", "/train-legacy", "使用旧版手动参数执行 RVC 训练"},
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
    if (quote) throw std::invalid_argument("参数中的引号没有闭合，请检查文件路径");
    if (active) parts.push_back(word);
    return parts;
}
int number(const std::string& value, int low, int high) {
    std::size_t used = 0;
    if (value.empty()) throw std::invalid_argument("Missing number");
    int n = 0;
    try { n = std::stoi(value, &used); }
    catch (const std::exception&) {
        throw std::invalid_argument("请输入有效整数：" + value);
    }
    if (used != value.size() || n < low || n > high)
        throw std::invalid_argument("数字超出允许范围（" + std::to_string(low) + "～" +
                                    std::to_string(high) + "）：" + value);
    return n;
}
fs::path as_path(const std::string& s) {
    if (s.empty()) throw std::invalid_argument("文件或目录路径不能为空");
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
// 标题与状态信息统一中文；引擎技术名称保持官方名称便于识别。
std::string menu_label(ConsoleMenu menu) {
    if (menu == ConsoleMenu::SeedVC) return "Seed-VC";
    if (menu == ConsoleMenu::Rvc) return "RVC";
    return "主菜单";
}
}  // namespace

CommandShell::CommandShell() {
    rvc_request_.mode = Mode::Rvc;
    seed_training.engine = TrainingEngine::SeedVC;
    seed_training.name = "seed_voice";
    seed_training.save_every = 200;
}
TrainingRequest CommandShell::active_training() const {
    if (menu_ == ConsoleMenu::Main) throw std::logic_error("请先选择 /Seed-VC 或 /RVC 引擎");
    return menu_ == ConsoleMenu::SeedVC ? seed_training : training;
}
TrainingConfiguration CommandShell::active_training_configuration() const {
    if (menu_ == ConsoleMenu::Main) throw std::logic_error("请先选择 /Seed-VC 或 /RVC 引擎");
    return menu_ == ConsoleMenu::SeedVC ? seed_training_configuration : training_configuration;
}

// 显示菜单名称；命令路由仍以 ConsoleMenu 枚举判断，不依赖翻译后的文本。
std::string CommandShell::menu_name() const { return menu_label(menu_); }
const Configuration& CommandShell::conversion_configuration() const {
    if (menu_ == ConsoleMenu::Main) throw std::logic_error("请先输入 /Seed-VC 或 /RVC 进入子菜单");
    return menu_ == ConsoleMenu::SeedVC ? seed_configuration_ : rvc_configuration_;
}
const Request& CommandShell::conversion_request() const {
    if (menu_ == ConsoleMenu::Main) throw std::logic_error("请先输入 /Seed-VC 或 /RVC 进入子菜单");
    return menu_ == ConsoleMenu::SeedVC ? seed_request_ : rvc_request_;
}
CommandResult CommandShell::set_option(const std::string& command, const std::string& value) {
    try {
        const auto key = lower_ascii(command);
        if (!allowed(menu_, key))
            throw std::invalid_argument("当前" + menu_name() + "不支持命令 " + command + "，请输入 /help 查看可用命令");
        if (menu_ == ConsoleMenu::Main) throw std::invalid_argument("请先选择 /Seed-VC 或 /RVC 引擎");
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
                throw std::invalid_argument("/device 只能填写 auto（自动）、cuda（显卡）或 cpu（处理器）");
            tr.device = value;
        } else if (key == "/offline") {
            if (value != "on" && value != "off")
                throw std::invalid_argument("/offline 只能填写 on（禁止下载）或 off（允许下载）");
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
            if (value != "on" && value != "off") throw std::invalid_argument("/overwrite 只能填写 on（允许覆盖）或 off（禁止覆盖）");
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
                else throw std::invalid_argument("/engine 只能填写 speech（讲话 V2）或 singing（歌声 V1）");
            } else if (key == "/steps") seed_request_.diffusion_steps = number(value, 1, 200);
            else throw std::invalid_argument("Seed-VC 子菜单不支持该参数：" + key);
        } else {
            if (key == "/rvc-root") {
                rvc_configuration_.rvc_root = as_path(value);
                training_configuration.rvc_root = as_path(value);
            } else if (key == "/model") rvc_request_.trained_model = as_path(value);
            else if (key == "/train-script") training_configuration.bridge_script = as_path(value);
            else if (key == "/index") {
                if (value != "on" && value != "off") throw std::invalid_argument("/index 只能填写 on（生成索引）或 off（不生成索引）");
                training.build_index = value == "on";
            } else throw std::invalid_argument("RVC 子菜单不支持该参数：" + key);
        }
        return {CommandAction::None, "已设置 " + key + "：" + value, true};
    } catch (const std::exception& e) {
        return {CommandAction::None, e.what(), false};
    }
}

CommandResult CommandShell::execute(const std::string& line) {
    try {
        const auto args = tokenize(trim(line));
        if (args.empty()) return {};
        const auto& key = args.front();
        if (key.empty() || key[0] != '/') throw std::invalid_argument("命令必须以 / 开头；输入 /help 查看用法");
        const auto normalized = lower_ascii(key);
        if (!allowed(menu_, normalized))
            throw std::invalid_argument("命令 " + key + " 不属于" + menu_name() + "；输入 /help 查看支持的命令");
        const bool no_value = normalized == "/seed-vc" || normalized == "/rvc" ||
            normalized == "/back" || normalized == "/help" || normalized == "/show" ||
            normalized == "/convert" || normalized == "/train" || normalized == "/train-legacy" ||
            normalized == "/train-log" || normalized == "/models" ||
            normalized == "/quality-report" || normalized == "/unload-model" ||
            normalized == "/quit";
        if (no_value) {
            if (args.size() != 1) throw std::invalid_argument(key + " 不需要附加参数");
            if (normalized == "/seed-vc") { menu_ = ConsoleMenu::SeedVC; return {CommandAction::None, "已进入 Seed-VC 菜单。输入 /help 查看专属命令。", true}; }
            if (normalized == "/rvc") { menu_ = ConsoleMenu::Rvc; return {CommandAction::None, "已进入 RVC 菜单。输入 /help 查看专属命令。", true}; }
            if (normalized == "/back") { menu_ = ConsoleMenu::Main; return {CommandAction::None, "已返回主菜单。", true}; }
            if (normalized == "/help") return {CommandAction::None, help(), true};
            if (normalized == "/show") return {CommandAction::None, summary(), true};
            if (normalized == "/quit") return {CommandAction::Quit, "已退出程序", true};
            if (normalized == "/convert") return {CommandAction::Convert, "正在使用 " + menu_name() + " 转换声音...", true};
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
                if (!input) return {CommandAction::None, "尚未生成训练日志：" + as_string(log_path), false};
                input.seekg(0, std::ios::end);
                const std::streamoff end = input.tellg();
                if (end <= 0) return {CommandAction::None, "训练日志目前为空", true};
                input.seekg(std::max<std::streamoff>(0, end - 1600));
                std::string tail{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
                return {CommandAction::None, tail, true};
            }
        }
        if (args.size() != 2) throw std::invalid_argument("命令只接受一个参数；包含空格的路径请用双引号包围");
        return set_option(key, args[1]);
    } catch (const std::exception& e) { return {CommandAction::None, e.what(), false}; }
}

// /help：按当前子菜单显示中文用法，不暴露其他引擎专属命令。
std::string CommandShell::help() const {
    std::ostringstream out;
    out << "myVoice / " << menu_name() << " · 中文操作帮助\n";
    out << "输入 / 查看命令提示；Tab 自动补全；回车执行；上下方向键查看历史命令。\n\n";
    const auto set = command_set(menu_);
    for (auto it = set.first; it != set.last; ++it)
        out << "  " << it->usage << "  ——  " << it->description << '\n';
    if (menu_ == ConsoleMenu::Main) {
        out << "\n使用顺序：先输入 /Seed-VC 或 /RVC，再进行声音转换或模型训练。\n";
    } else if (menu_ == ConsoleMenu::SeedVC) {
        out << "\n讲话转换示例：/engine speech → /source → /target → /output → /convert\n";
        out << "V2 微调示例：/dataset → /train-name → /train → /models → /load-model\n";
        out << "说明：/engine singing 使用 V1 歌声模型，不能加载 V2 CFM/AR 检查点。\n";
        out << "输入 /back 返回主菜单；带空格的文件路径请用双引号包围。\n";
    } else {
        out << "\n自动训练示例：/dataset → /train-name → /device auto → /train\n";
        out << "使用模型示例：/models → /load-model → /source → /output → /convert\n";
        out << "说明：/train-legacy 为旧版手工训练模式，/offline on 禁止联网下载权重。\n";
        out << "输入 /back 返回主菜单；带空格的文件路径请用双引号包围。\n";
    }
    return out.str();
}

// /show：统一使用中文字段名；显示值仍使用原有英文枚举，确保与输入参数一致。
std::string CommandShell::summary() const {
    std::ostringstream o;
    o << "当前菜单：" << menu_name();
    if (menu_ == ConsoleMenu::Main)
        return o.str() + "\n请选择 /Seed-VC（讲话/歌声转换、V2 微调）或 /RVC（训练、变声）。";
    const auto& cfg = conversion_configuration();
    const auto& req = conversion_request();
    o << "\n转换模式：" << (req.mode == Mode::Speech ? "speech（讲话 V2）" :
                              req.mode == Mode::Singing ? "singing（歌声 V1）" : "RVC")
      << "\n源音频：" << as_string(req.source)
      << "\n输出音频：" << as_string(req.output)
      << "\nPython 运行环境：" << (cfg.python.empty() ? "自动使用仓库内置便携 Python" : as_string(cfg.python))
      << "\n转换超时：" << cfg.timeout.count() / 1000 << " 秒"
      << "\n允许覆盖输出：" << (req.overwrite ? "是（on）" : "否（off）");
    if (menu_ == ConsoleMenu::SeedVC) {
        o << "\nSeed-VC 源码目录：" << (cfg.seed_vc_root.empty() ?
                                          "仓库内置 third_party/seed-vc" : as_string(cfg.seed_vc_root))
          << "\n参考音频：" << as_string(req.target)
          << "\n扩散推理步数：" << req.diffusion_steps
          << "\nSeed-VC V2 CFM 权重：" << as_string(req.seed_cfm_model)
          << "\nSeed-VC V2 AR 权重：" << as_string(req.seed_ar_model)
          << "\n训练素材目录：" << as_string(seed_training.dataset)
          << "\n训练模型名称：" << seed_training.name
          << "\n最大微调步数：" << seed_training.steps
          << " | 训练设备：" << seed_training.device;
    } else {
        o << "\nRVC 源码目录：" << (training_configuration.rvc_root.empty() ?
                                     "仓库内置 third_party/rvc" : as_string(training_configuration.rvc_root))
          << "\n当前加载的模型：" << as_string(req.trained_model)
          << "\n训练素材目录：" << as_string(training.dataset)
          << "\n训练模型名称：" << training.name
          << "\n训练轮数：" << training.epochs
          << " | 批大小：" << (training.batch_size == 0 ? "自动（auto）" : std::to_string(training.batch_size))
          << " | 加载线程数：" << (training.workers == 0 ? "自动（auto）" : std::to_string(training.workers))
          << "\n训练设备：" << training.device
          << " | GPU 编号：" << (training.gpu < 0 ? "自动（auto）" : std::to_string(training.gpu))
          << " | 生成索引：" << (training.build_index ? "是（on）" : "否（off）");
    }
    return o.str();
}

// 枚举参数的候选值仍使用命令行约定的英文，右侧添加中文解释。
std::string suggestion_label(const std::string& key, const std::string& value) {
    if (key == "/engine")
        return value == "speech" ? "speech（普通讲话，V2）" : "singing（歌声，V1）";
    if (key == "/device") {
        if (value == "auto") return "auto（自动检测训练设备）";
        return value == "cuda" ? "cuda（NVIDIA CUDA 显卡）" : "cpu（处理器）";
    }
    if (key == "/offline")
        return value == "on" ? "on（仅使用本地权重）" : "off（允许下载缺失权重）";
    if (key == "/overwrite")
        return value == "on" ? "on（允许覆盖输出文件）" : "off（禁止覆盖输出文件）";
    if (key == "/index")
        return value == "on" ? "on（生成检索索引）" : "off（不生成索引）";
    return value;
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
        for (const auto& c : choices)
            if (starts(c, prefix))
                result.push_back({replacement(key, c), suggestion_label(key, c)});
    } else if (key == "/load-model") {
        // 仅提示当前引擎已登记的模型，防止 RVC 与 Seed-VC 的模型混用。
        try {
            for (const auto& model : read_model_registry(default_model_registry(),
                     menu_ == ConsoleMenu::SeedVC ? "seed-vc" : "rvc")) {
                if (starts(model.name, prefix))
                    result.push_back({replacement(key, model.name), model.name});
            }
        } catch (const fs::filesystem_error&) {}
    } else if (is_path(key)) {
        // 路径自动补全只显示用户可访问的文件和目录；补全后保留 /命令格式。
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
