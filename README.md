# myVoice

**C++17 (VS2022 / Windows / Linux) 零样本语音转换工程。** 输入源声音 `female.wav` 与目标人物参考声音 `male.wav`，输出同样内容、目标音色的 `result.wav`，**不需要训练 RVC、不需要麦克风**。

当前实现将模型推理委托给本机安装的 [Seed-VC](https://github.com/Plachtaa/seed-vc)：**讲话用 Seed-VC V2**（`inference_v2.py`）；**歌声用 Seed-VC V1 F0 条件模型**（`inference.py`）。C++ 负责参数校验、原生子进程启动、超时、隔离临时目录、识别和校验输出音频。模型代码/权重不随仓库分发。

> 注意：此版本是**文件离线推理**，不是实时麦克风、RVC 训练器或纯 C++ 神经网络。默认没有自定义自动音域映射；唱歌时关闭自动移调，保留原始旋律。这些限制是刻意保留的，避免把未经验证的高音修复功能当作已实现。

## 编译

需要 CMake >= 3.24（预设文件要求）、C++17 编译器。仅对 Windows (Visual Studio 2022 x64) 和 Linux 提供支持，C++ 端**无需第三方库**。

### Windows / VS2022

安装带 **Desktop development with C++** 的 Visual Studio 2022 与 CMake。仓库可直接通过 VS2022 的 **Open Folder** 打开，选择 `windows-vs2022` 预设；也可以在 VS2022 Developer PowerShell 中运行：

```powershell
cmake --preset windows-vs2022
cmake --build --preset windows-vs2022
ctest --preset windows-vs2022
```

生成的 CLI 在 `build/vs2022/Release/myvoice_cli.exe`。若希望生成独立 `.sln`，执行预设配置后打开 `build/vs2022/myVoice.sln`。

### Linux

```bash
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
```

生成的 CLI 在 `build/linux/myvoice_cli`。

## 安装真正的推理后端（首次配置）

1. 按 [Seed-VC 官方 README](https://github.com/Plachtaa/seed-vc) 克隆项目并配置 Python 3.10 + PyTorch（建议 NVIDIA GPU/CUDA）。项目原始仓库已归档，依赖版本需要与你的 Python/PyTorch 环境匹配。
2. 在 Seed-VC 目录创建独立虚拟环境并按官方说明安装 `requirements.txt` 与预训练权重；第一次调用可能自动下载模型文件。Linux CUDA / Windows CUDA 的安装方式以官方说明为准。
3. 将 `--seed-root` 指向 **包含 `inference.py` 和 `inference_v2.py` 的 Seed-VC 仓库根目录**，`--python` 指向该虚拟环境的 Python 可执行文件。
4. 推荐先准备清晰、无伴奏、无削波的 WAV 源文件和目标参考录音；通常使用数秒到数十秒清晰参考声音更容易得到稳定结果。实际效果随模型、发音、音域与显卡而变化。

### Windows 调用示例（直接使用文件，无麦克风）

```powershell
.\build\vs2022\Release\myvoice_cli.exe `
  --python "D:\Seed-VC\.venv\Scripts\python.exe" `
  --seed-root "D:\Seed-VC" `
  --source "D:\audio\female.wav" `
  --target "D:\audio\male.wav" `
  --output "D:\audio\result.wav" `
  --engine speech
```

### Linux 调用示例

```bash
./build/linux/myvoice_cli \
  --python /home/user/seed-vc/.venv/bin/python \
  --seed-root /home/user/seed-vc \
  --source /home/user/audio/female.wav \
  --target /home/user/audio/male.wav \
  --output /home/user/audio/result.wav \
  --engine speech
```

### 高音唱歌（保持原旋律）

把 `--engine speech` 改为 `--engine singing`，即调用 Seed-VC V1 的 `--f0-condition True --auto-f0-adjust False --semi-tone-shift 0`。可追加 `--steps 40` 测试不同的推理质量与时间；高音失真**不会因更换模式而保证完全消失**。

CLI 其他参数：`--timeout 900`（秒）、`--overwrite`（允许替换已存在的**输出**文件）、`--steps 30`（推理步数，1..200）。源文件和目标文件不能与输出路径相同。输出路径的父目录需提前创建。

## C++ 接口示例

```cpp
#include <myvoice/VoiceConverter.h>

myvoice::Configuration config;
config.python = "/path/to/seed-vc/.venv/bin/python";
config.seed_vc_root = "/path/to/seed-vc";

myvoice::Request req;
req.source = "female.wav";
req.target = "male.wav";
req.output = "result.wav";
req.mode = myvoice::Mode::Speech;  // or Singing

const auto result = myvoice::VoiceConverter(config).convert(req);
```

链接 `myvoice::core`（CMake target），或者 `myvoice_core`。函数同步执行，异常使用标准 C++ 异常上报。应用若需异步 UI 可用线程池调度，不要在界面线程阻塞等待 Python。每次调用会启动一个 Python 子进程并重新加载模型，适合第一版文件推理；连续转换可后续增加常驻服务以降低延迟。

## 工程目录

```text
include/myvoice/VoiceConverter.h  公共 C++17 API
src/VoiceConverter.cpp           音频文件校验/模型选择/产物管理
src/Process.cpp                  Windows CreateProcessW / Linux fork+execvp
apps/myvoice_cli.cpp             可直接运行的命令行 Demo
tests/myvoice_tests.cpp          无 PyTorch 的模拟模型集成测试
.github/workflows/ci.yml        VS2022 + Linux 自动编译与测试
CMakePresets.json               VS2022 / Linux 项目配置
```

## 测试说明与限制

CI 使用模拟的 `inference.py` / `inference_v2.py`，检验跨平台启动、路径空格、讲话与歌声路由、错误退出、超时、禁止覆盖输入等逻辑。**CI 不下载模型、不实际测试声音相似度，也不意味着真实音频推理已经通过验证。** 必须在有合适 GPU 和真实模型的机器上手工试听普通讲话、高音讲话、假声与高音歌唱片段。

Windows 的超时会终止直接子进程；Linux 使用独立进程组终止进程组。Windows 当前不承诺终止由 Python 额外派生的整个进程树。

## 许可与合规

本仓库 C++ 包装层使用 MIT 许可证；**Seed-VC 是独立的 GPL-3.0 项目，模型权重和其他依赖还有各自许可证。** 当前仓库不复制 Seed-VC 源码或预训练权重。若打包/再分发上游模型，请独立核对其许可证义务。请在获得目标声音所有者授权后进行声音克隆。
