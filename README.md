# myVoice

C++17、Visual Studio 2022、Windows/Linux 的 **FTXUI 交互式语音转换控制台**。输入原始音频文件和目标声音参考文件，调用本地 [Seed-VC](https://github.com/Plachtaa/seed-vc) 模型生成目标音色音频。**不需要麦克风或提前训练 RVC**。

- **FTXUI 控制台**：无参数运行 `myvoice_cli`，提供输入面板、消息记录、推理状态提示。
- **统一 `/` 命令**：交互模式和批处理模式都使用 `/source`、`/target`、`/output` 等。
- **自动提示/补全**：输入 `/` 显示候选命令；输入 `/eng` 提示 `/engine`；输入 `/engine s` 提示 `speech/singing`；路径参数支持文件名补全。**Tab** 填充第一个匹配项，**Enter** 执行，**↑/↓** 查看历史。
- **异步调用**：FTXUI 的推理工作放在后台线程，执行期间界面仍可响应；Python 的 stdout/stderr 会被隔离，失败时显示末尾日志；内部仍由 C++ 创建本地 Python 子进程。
- **引擎**：`speech` → Seed-VC V2，`singing` → Seed-VC V1 F0 条件模型，歌声默认不移调。

**注意**：这个版本没有自动音域映射/失真修复模块，真实音质需要使用模型权重与音频进行验证。Seed-VC 推理依赖及权重需另外安装，未直接捆绑到本仓库。

## 一、编译

需要 CMake >= 3.24 和 C++17 编译器。FTXUI 版本固定为 **v7.0.3**（MIT），CMake 优先寻找本机安装版本，未找到时通过 FetchContent 下载；首次配置需要联网。也可设置 `FETCHCONTENT_SOURCE_DIR_FTXUI` 指向已下载的源码进行离线构建。

### Windows / VS2022

安装 VS2022 **使用 C++ 的桌面开发** 工作负载；用 Visual Studio **打开文件夹**选择 `windows-vs2022` 配置，或在 Developer PowerShell 执行：

```powershell
cmake --preset windows-vs2022
cmake --build --preset windows-vs2022
ctest --preset windows-vs2022
```

生成 `build/vs2022/myVoice.sln` 和 `build/vs2022/Release/myvoice_cli.exe`。

### Linux

```bash
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
```

生成 `build/linux/myvoice_cli`。

没有网络、只需要构建 C++ 核心和运行命令解析/模拟推理测试时，可额外设置 `-DMYVOICE_WITH_FTXUI=OFF`；此时不构建交互界面，但仍支持 `/` 命令批处理。

## 二、安装 AI 模型

参照 [Seed-VC README](https://github.com/Plachtaa/seed-vc) 安装 Python 3.10、PyTorch 及 `requirements.txt`，准备模型权重（按官方说明下载或首次运行时获取）。这里需要将 `/seed-root` 设置为**包含** `inference.py` 和 `inference_v2.py` **的根目录**，`/python` 设置为对应虚拟环境解释器。建议有 NVIDIA CUDA 显卡。

## 三、FTXUI 交互控制台

直接运行：

```powershell
.\build\vs2022\Release\myvoice_cli.exe
```

进入界面后输入以下命令，**每行输入一条后回车**：

```text
/python "D:\Seed-VC\.venv\Scripts\python.exe"
/seed-root "D:\Seed-VC"
/source "D:\audio\female.wav"
/target "D:\audio\male.wav"
/output "D:\audio\converted.wav"
/engine speech
/steps 30
/show
/convert
```

如果转换高音歌声：使用 `/engine singing`。默认将歌声音高保持原样，不会强制把女声降一个八度。输出文件已存在时可先输入 `/overwrite on`（不会覆盖原始输入文件）；`/quit` 退出控制台。

### 自动提示与快捷键

| 输入/按键 | 行为 |
| --- | --- |
| `/` | 列出可用命令及说明 |
| `/en` | 提示 `/engine` |
| `/engine s` | 提示 `speech`、`singing` 中符合前缀的项 |
| `/source D:/au` | 列出符合前缀的文件和目录（存在时） |
| `Tab` | 自动填入当前第一个候选项 |
| `Enter` | 执行命令 |
| `↑`、`↓` | 浏览已输入的命令历史 |
| `/help` | 查看全部命令和参数用法 |
| `/show` | 显示当前设置 |

有空格的路径必须用引号括起，如 `/source "D:\voice files\input.wav"`；Windows 路径反斜杠不会被作为转义字符剥离。

## 四、自动化调用（无 FTXUI 交互）

只要提供音频输入参数，CLI 就运行一次转换，并在完成后返回退出码。所有选项都使用 `/`，没有 `--` 前缀。

**Windows PowerShell**：

```powershell
.\build\vs2022\Release\myvoice_cli.exe `
  /python "D:\Seed-VC\.venv\Scripts\python.exe" `
  /seed-root "D:\Seed-VC" `
  /source "D:\audio\female.wav" `
  /target "D:\audio\male.wav" `
  /output "D:\audio\result.wav" `
  /engine speech
```

**Linux**：

```bash
./build/linux/myvoice_cli \
  /python /home/user/seed-vc/.venv/bin/python \
  /seed-root /home/user/seed-vc \
  /source /home/user/female.wav \
  /target /home/user/male.wav \
  /output /home/user/result.wav \
  /engine singing /steps 40
```

`/timeout 900` 设置超时秒数，`/overwrite on` 允许输出已存在时覆盖（保护输入文件）。`/help` 可在无模型环境下查询帮助；`/interactive` 可以显式进入 FTXUI。

## 五、C++ API

```cpp
#include <myvoice/VoiceConverter.h>
myvoice::Configuration config;
config.seed_vc_root = "D:/Seed-VC";
config.python = "D:/Seed-VC/.venv/Scripts/python.exe";
myvoice::Request req;
req.source = "female.wav";
req.target = "male.wav";
req.output = "converted.wav";
req.mode = myvoice::Mode::Speech;
auto result = myvoice::VoiceConverter(config).convert(req);
```

核心接口独立于 FTXUI，可以通过 `myvoice::CommandShell` 在其他应用中复用 `/` 命令解析和自动提示。C++ 核心本身不依赖 FTXUI 或 PyTorch。

## 六、测试和边界

CI 在 Linux 和 Windows Server 2022 编译、运行纯 C++ 命令解析及自动补全测试、模拟 Python 后端测试（包括路径空格、超时、失败退出、输出保护）和 `/help` `/show` CLI 测试。**CI 不运行真实 Seed-VC 权重，不能由此认定音色转换效果已经验证。**

FTXUI 提供异步控制台，但 Python 子进程一次调用会重新加载模型，不属于实时麦克风变声。Windows 目前超时只保证终止直接 Python 子进程，不保证结束它衍生的所有进程；Linux 使用独立进程组终止。

仓库 C++ 代码使用 MIT。FTXUI 使用 MIT；Seed-VC 为独立 GPL-3.0 上游项目，模型和第三方依赖另有许可证。请遵守上游许可并获得目标声音所有者的授权。
