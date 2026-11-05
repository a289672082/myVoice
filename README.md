# myVoice

Windows / Linux 跨平台 C++17 + FTXUI 语音转换程序，附带可直接打开的 Visual Studio 2022 `myVoice.sln`。

**FTXUI、RVC、Seed-VC 的源码都已直接检入本仓库**，位于 `third_party/`，不是 Git 子模块，不需要 CMake/运行时从 GitHub 下载引擎源码。FTXUI 为 MIT；RVC 为 MIT；Seed-VC 为 GPL-3.0，详见各自 `LICENSE`。请分别遵守对应许可。

**重要限制：** 此次内置的是源码，便携 Python 运行包单独通过同仓库 Releases 分发，而不是完整的离线 AI 运行环境。PyTorch、CUDA、Python 第三方包、HuBERT/RMVPE、Seed-VC 所需检查点等大型预训练模型权重仍需要单独准备；因此 **C++ 编译离线可行，首次模型运行不保证离线**。不包含第三方示例歌曲或人物录音。

| 引擎 | 默认源码目录 | 功能 |
|---|---|---|
| FTXUI | `third_party/ftxui/` | TUI 命令控制台 |
| RVC | `third_party/rvc/` | 已训练模型推理、RVC V2 训练 |
| Seed-VC | `third_party/seed-vc/` | V2 讲话、V1 歌声零样本转换 |

两个引擎使用自己独立的便携 Python 环境和权重文件；`/rvc-root`、`/seed-root` 是可选覆盖参数，不再要求单独克隆源码仓库。使用 `/show` 查看当前引擎参数。训练出来的 RVC 模型不能直接由 Seed-VC 加载。

Windows / Linux 跨平台 **C++17 + FTXUI** 语音转换与 **RVC V2 目标音色训练** 项目。可在 **Visual Studio 2022** 中使用 CMake 工程（自动生成 `.sln`）。命令统一以 `/` 开头。**先输入 `/Seed-VC` 或 `/RVC` 进入引擎专属子菜单**，再执行该引擎的命令；支持实时命令提示、Tab 补全、文件路径补全和历史命令。


## 免安装 Python 完整运行包（Windows / Linux）

**不是安装包**：每个压缩包包含对应平台的 CPython 可执行程序、标准库及构建阶段安装的第三方 Python 依赖（含 CPU 版 PyTorch / Torchaudio），解压即可由 myVoice 调用。RVC 与 Seed-VC 使用独立的目录，避免依赖版本相互冲突：

```text
runtime/
  windows-x64/
    rvc/python.exe
    seed-vc/python.exe
  linux-x64/
    rvc/bin/python3
    seed-vc/bin/python3
```

这些二进制依赖体积通常数百 MB 至数 GB，GitHub 普通 Git 文件有大小限制。因此 **Python 运行包作为同一 myVoice 仓库的 GitHub Release 附件发布**，不会将巨型二进制写入 Git 历史。源码、生成脚本和清单都在本仓库，下载或解压后就位于本仓库根目录的 `runtime/` 里。可直接下载对应 ZIP 解压，或使用仓库附带的下载命令：

```powershell
# Windows PowerShell，无须预先安装 Python
.\scripts\get_portable_python.ps1 -Engine all
```

```bash
# Linux x86-64，需要 curl 或 wget 和 unzip；无须系统 Python
bash scripts/get_portable_python.sh all
```

然后在 FTXUI 内进入 `/Seed-VC` 或 `/RVC`，直接配置音频/模型并使用 `/convert`、`/train`，**无需输入 `/python`、也不会回退到系统 Python**。如需自定义解释器，原有 `/python` 仍有效。

[查看 Portable Python 发布页面](https://github.com/a289672082/myVoice/releases/tag/portable-python-v1)；[构建及来源说明](runtime/README.md)。

**边界：** 提供的是 Python + Python 扩展包的 CPU 版本；NVIDIA CUDA 驱动、GPU 运行包，以及 RVC/Seed-VC 的 HuBERT/RMVPE/Seed-VC 预训练权重不在这些压缩包中。CPU 可用于先验证流程，但实际训练与长音频推理可能很慢。Python 依赖的复制与重定位必须经过 Windows/Linux 运行测试；未发布通过校验的附件之前不能称为已完成完整免安装部署。

## 新手训练向导（推荐）

**不熟悉 Python、CUDA、模型参数也可以从这里开始。** 启动 FTXUI，进入 **/RVC** 或 **/Seed-VC**，输入：

```text
/RVC
/train-wizard
```

向导按中文提示依次询问：①训练录音目录；②模型名称；③设备（直接回车默认 `auto`）；④可选试听样本 WAV（回车跳过，Seed-VC 还会询问目标参考录音）；⑤核对配置并输入 **确认**。输入 `/cancel` 可随时退出，取消时不会修改已有训练参数；**最后确认前不会开始训练或下载大型权重**。Seed-VC 内同样支持 `/train-wizard`，专门运行 V2 CFM 微调。

### GPU 自动检查与配置

如果电脑有 NVIDIA 显卡，建议在开始训练前执行：

```text
/RVC
/gpu-check
/gpu-setup
/train-wizard
```

- `/gpu-check`：只读 `nvidia-smi` 检查 GPU 型号、驱动和计算能力，**不下载文件**。
- `/gpu-setup`：用户明确执行后下载官方固定版本的 uv 与 PyTorch CUDA 12.1，复制仓库 CPU 便携 Python 到独立的 `runtime/windows-x64/rvc-gpu/`（Linux 相应目录），在新环境中安装 CUDA 依赖并做真实 GPU 张量测试。**原 CPU Python 不会被覆盖**。
- 验证通过后生成 `myvoice-gpu-ready.json`，后续训练/推理自动优先使用该 GPU Python。Seed-VC 对应 `/Seed-VC` 子菜单与 `seed-vc-gpu/`。
- 检测到不支持的显卡、旧驱动或下载/安装失败时明确报错，并保持 CPU 运行环境可用；不会自动安装或更新显卡驱动。GPU 包可能需要几 GB 磁盘及大量下载流量。若 PyTorch 2.4.x 不支持较新显卡（例如部分 RTX 50 系列），请等待经验证的新版本，不强行安装。

### 训练进度与试听

开始训练后可输入 `/train-status` 查看阶段（**音频筛选 → GPU/CPU 检测 → 下载权重 → 执行训练 → 注册模型**），或 `/train-log` 查看底层日志。FTXUI 在训练期间会定时刷新当前阶段；**阶段 4/5 可能耗时较长，阶段编号不是准确的剩余时间百分比**。

成功后：

```text
/models
/load-model male_v1
/source "D:\audio\female.wav"
/output "D:\audio\result.wav"
/convert
```

模型会自动登记到 `models/index.tsv`，无需自行查找 `.pth`。若在向导中填写测试音频，将在训练成功后自动生成独立的 `models/previews/*_preview.wav` 供试听，**不会自动播放、也不会覆盖已有文件**。Seed-VC 微调后的模型只用于 `/engine speech`，试听转换仍需 `/target` 参考录音。普通 WAV 试听文件可用系统音频播放器打开，程序不会自动执行或打开任何外部文件。

**限制**：训练向导可减少常规配置，但不能保证所提供录音的版权、说话人身份、无伴奏或最终音色质量；也无法保证所有显卡都支持固定的 PyTorch/CUDA 组合。对真实 GPU 的完整训练、断点续训和端到端音质仍需在用户硬件上验证。


## 智能训练与模型管理（RVC / Seed-VC）

当前实现将以下步骤组合为一个命令：**自动筛查音频 → 检测 PyTorch 可用 GPU / 显存 → 按需下载官方预训练资源 → 调用对应官方训练脚本 → 检验输出 → 注册到 `models/index.tsv`**。

### RVC 一键训练

将得到合法授权的目标音色 WAV/FLAC 等音频放进一个目录，在 FTXUI 输入：

```text
/RVC
/dataset "D:\voice_data\male"
/train-name male_v1
/device auto
/batch auto
/gpu auto
/train
```

训练完成后：

```text
/models
/load-model male_v1
/source "D:\audio\female.wav"
/output "D:\audio\result.wav"
/convert
```

RVC 训练结果为 `third_party/rvc/assets/weights/male_v1.pth`，索引文件通常位于 RVC 资产目录。旧版手工调参训练可使用 `/train-legacy`（须显式设置 batch、workers、gpu、device）。

### Seed-VC V2 专属微调

Seed-VC 的模型不能使用 RVC 权重。进入它自己的菜单：

```text
/Seed-VC
/dataset "D:\voice_data\male"
/train-name male_seed_v2
/train-steps 1000
/device auto
/batch auto
/train
```

这会调用内置 Seed-VC `train_v2.py` 微调 **CFM**（AR 默认保留官方基础权重）。输出一般为 `third_party/seed-vc/runs/male_seed_v2/CFM_epoch_*_step_*.pth`，完成后自动登记 CFM 和对应 AR 文件。

```text
/models
/load-model male_seed_v2
/engine speech
/source "D:\audio\female.wav"
/target "D:\audio\male_reference.wav"
/output "D:\audio\seed_result.wav"
/convert
```

**Seed-VC V2 即使加载微调权重，仍然需要目标音色参考录音 `/target`。** 如需切换到 V1 歌声模式，先执行 `/unload-model` 清除 V2 检查点，再输入 `/engine singing`。 这些 V2 检查点只能通过 `speech` 加载，不能用于 Seed-VC V1 `singing`。

### 自动管理的边界

- `/quality-report` 显示 `myvoice_quality_report.json` 路径。自动筛选过短/过长、明显静音、明显削波、直流偏移、不可解码文件，并标记可能持续背景声的警告。**这不是人工音质检测，也不保证识别所有伴奏、错误说话人或合成伪影**。
- `/device auto` 使用当前 Python/PyTorch 实际可识别的 CUDA 设备和显存决定批次；`/batch auto`、`/gpu auto` 允许自动配置。现有 Releases 提供的是 **CPU 版 PyTorch**，无法凭 NVIDIA 驱动自动获得 CUDA：GPU 训练仍需匹配的 CUDA PyTorch 运行环境（可用各引擎 `/python` 指定）。
- 缺少预训练资源时，将从官方 Hugging Face 项目获取并复用缓存；可设置 `/offline on` 禁止网络下载，缺失时报错。网络下载只针对预定义模型，不使用不可信的任意权重 URL。
- RVC 核心预训练模型包括 HuBERT、RMVPE、V2 F0 Generator/Discriminator；Seed-VC V2 的 CFM、AR 和编码器/说话人检查点也会提前检查。官方模型内部的 Whisper/BigVGAN 等依赖可能另行按需下载。
- 训练输出成功并检查文件存在之后才会登记模型。索引是 `models/index.tsv`，无需手工移动权重；`/models` 列表及 `/load-model 名称` 支持 Tab 补全。登记索引和训练数据不会加入 Git。
- 实际 GPU 训练、最终音色/高音质量未在 CI 中完成验证。CI 验证的是 Windows/Linux C++ 编译、独立训练菜单、音质分析和**模拟训练、权重下载、模型注册**。

## 功能

| 功能 | 当前实现 |
| --- | --- |
| FTXUI 控制台、`/Seed-VC` 与 `/RVC` 子菜单、Tab 自动补全 | ✅ |
| Windows / VS2022、Linux 编译 | ✅ CMake |
| `/engine speech` | Seed-VC V2 零样本普通语音转换 |
| `/engine singing` | Seed-VC V1 带 F0 条件歌声转换（默认不移调） |
| `/train` | RVC 自动训练；Seed-VC V2 CFM 微调；自动音质筛选、GPU、权重下载、注册模型 |
| `/RVC` + `/model` / `/load-model` | 使用训练后的 RVC `.pth` 文件进行离线推理 |
| `/train-log` | 查看 RVC / Seed-VC 对应训练日志尾部 |
| `/models`、`/load-model` | 分引擎模型登记、列表与加载 |
| 高音失真自动修复 / 自动音域映射 | **未实现**，不能保证消除女声高音转男声失真 |
| 实时麦克风、GUI 训练参数面板、纯 C++ 模型训练 | **未实现** |

自动训练会调用 RVC/Seed-VC 各自的 Python 训练脚本，C++ 负责参数校验、子进程调度、超时、日志和 FTXUI 操作。**未经真实 GPU、模型权重及语音样本验证最终音质**，仅已完成模拟训练/转换测试。

## VS2022 原生解决方案（可直接双击）
  
仓库根目录已包含 **`myVoice.sln`** 和 **`vs2022/*.vcxproj`**。打开 `myVoice.sln`，在 VS2022 选择 `x64`、`Debug` 或 `Release`，将 `myvoice_cli` 设为启动项目，按 **F5** 调试或按 **Ctrl+Shift+B** 编译。**无需先运行 CMake**。解决方案使用本仓库 `third_party/ftxui` 的全部本地源码，不下载 FTXUI，也不依赖 Python 安装来编译 C++。C++17 / MSVC v143 / Windows 10 SDK。

该解决方案包含 `myvoice_cli`、`myvoice_core`、`ftxui_component`、`ftxui_dom`、`ftxui_screen` 和 `myvoice_shell_tests`，工程引用自动决定依赖编译顺序，输出位于 `build/vs2022-native/x64/<Debug|Release>/`；已在工程中指定从仓库根目录启动调试，`/train-script` 仍可显式指定桥接脚本。

这是**真正检入仓库的 MSBuild 原生 VC++ 工程**，而不只是提示用 CMake 生成临时 `.sln`。原有 CMake 预设保持可用，适合 Linux 与跨平台 CI。源码变更后，可执行 `python tools/verify_vs2022.py` 检查解决方案列出的源文件是否与仓库一致；新增 FTXUI 文件时还需要同步更新对应 `.vcxproj`。

## 一、VS2022 编译（离线 CMake）

安装 Visual Studio 2022 的“使用 C++ 的桌面开发”工作负载，以及 CMake >= 3.24。在 Visual Studio 中使用 **打开文件夹**打开仓库根目录并选择 `windows-vs2022`，或运行：

```powershell
cmake --preset windows-vs2022
cmake --build --preset windows-vs2022
ctest --preset windows-vs2022
.\build\vs2022\Release\myvoice_cli.exe
```

Linux：

```bash
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
./build/linux/myvoice_cli
```

如只需命令行无 TUI，可以加 `-DMYVOICE_WITH_FTXUI=OFF`。FTXUI 源码无需重新下载。CI 同时构建两个平台并运行模拟测试。

## 二、RVC 旧版手动训练参考（/train-legacy）

1. 使用内置的 `third_party/rvc` 源码，单独安装 RVC 的 Python 环境及依赖（推荐可用 CUDA GPU），取得 `assets/hubert_base/`、`assets/rmvpe/rmvpe.pt`、`assets/pretrained_v2/f0G48k.pth`、`f0D48k.pth` 等**官方**预训练权重。训练桥接脚本只适配仓库中具有 `train/preprocess.py`、`train/dataset/extract_f0.py`、`train/dataset/extract_hubert_feature.py`、`train/train.py`、`train/train_index.py` 的版本。
2. 把得到授权的目标男声录音（建议有正常讲话和真实高音/唱歌内容）放到独立目录，比如 `D:\voice_data\male`。尽量避免音乐伴奏、噪声、明显削波和 TTS 合成伪影。训练阶段使用的是**目标男声**，不是要转换的女声音频。
3. 打开 FTXUI，先进入 `/RVC`，再输入训练参数：

```text
/RVC
/python "D:\myVoice\.venv-rvc\Scripts\python.exe"
/dataset "D:\voice_data\male"
/train-name male_v1
/epochs 200
/batch 4
/workers 4
/save-every 20
/device cuda
/gpu 0
/index on
/train-timeout 43200
/train-legacy
```

训练开始后状态栏显示 `TRAINING`，中途使用 `/train-log` 查看最近输出，完整日志位于 `D:\myVoice\third_party\rvc\logs\male_v1\myvoice_train.log`。RVC 的模型权重通常在 `D:\myVoice\third_party\rvc\assets\weights`，相关训练中间产物、检查点和索引位于 `D:\myVoice\third_party\rvc\logs\male_v1`，索引可链接到 `assets/indices`。

- `epochs`：1..10000，实际训练效果需对照评估检查点；不是越大越好。
- `batch`：1..128，显存不足时可降低；`/device cpu` 可用于排查/测试，但训练极慢。
- `index off`：仅训练权重，不构建 FAISS 索引。
- 旧版 /train-legacy 不会自动下载模型权重；智能 /train 则自动检查下载所需基础权重。**只有高音真实覆盖的合格训练素材才可能改善高音失真**。

**提示**：某些 RVC 版本的训练脚本/资源布局不同。本实现针对已核对的 `RVC-Project/Retrieval-based-Voice-Conversion-WebUI` 的 `train/*` 结构；不保证兼容所有老分支或第三方打包版。失败时可从 `/train-log` 找到阶段和底层错误。

## 三、使用训练好的 RVC 音色转换

训练完成并取得 `.pth` 文件后，进入 RVC 子菜单：

```text
/RVC
/python "D:\myVoice\.venv-rvc\Scripts\python.exe"
/model "D:\myVoice\third_party\rvc\assets\weights\male_v1.pth"
/source "D:\audio\female.wav"
/output "D:\audio\result.wav"
/convert
```

RVC 推理默认 RMVPE、`pitch=0`、`index-rate=0`，**不要求手工操作移调滑条**，也不需要再输入参考音频；但这并非自动基频校正。高音出现失真时，需要针对训练数据和转换模型实测。`/overwrite on` 可以覆盖已经存在的输出文件（但禁止覆盖源文件）。

## 四、免训练的 Seed-VC 转换

使用内置的 `third_party/seed-vc` 源码，为它准备 Python 环境和相应权重，然后进入 Seed-VC 子菜单：

```text
/Seed-VC
/python "D:\myVoice\.venv-seed\Scripts\python.exe"
/source "D:\audio\female.wav"
/target "D:\audio\male_reference.wav"
/output "D:\audio\result.wav"
/engine speech
/convert
```

唱歌时用 `/engine singing`，官方 F0 条件模型默认不移调，避免影响原始伴奏调性。

## 五、中文菜单注释、自动提示与批处理

**所有菜单、帮助信息和自动补全提示均使用中文解释**；命令本身仍以 `/` 开头，并保留 `/Seed-VC`、`/RVC`、`/train` 等已有名称。输入 `/help` 可以查看当前子菜单的**中文命令用途、参数和使用示例**；输入 `/show` 查看中文参数概览。

```text
myVoice / 主菜单 · 中文操作帮助
  /Seed-VC  ——  进入 Seed-VC 语音转换及 V2 微调子菜单
  /RVC      ——  进入 RVC 模型训练和语音转换子菜单
  /help     ——  显示当前菜单的中文命令帮助

> /Seed-VC
myVoice / Seed-VC
> /engine speech
> /source "D:\audio\female.wav"
> /target "D:\audio\male.wav"
> /output "D:\audio\converted.wav"
> /convert
> /back

myVoice / 主菜单
> /RVC
> /dataset "D:\voice_data\male"
> /train-name male_v1
> /device auto
> /train
> /models
> /load-model male_v1
```

- **Seed-VC 子菜单**：`/engine speech` 表示讲话（V2），`/engine singing` 表示歌声（V1）。`/train` 可微调 Seed-VC V2 CFM；`/models` 和 `/load-model` 可查看并加载训练好的权重；`/unload-model` 可卸载 V2 权重后切换到 V1 歌声模式。
- **RVC 子菜单**：`/train` 是自动检查录音质量、预训练权重及硬件的一键训练入口；`/train-legacy` 保留旧版手动训练；`/models`、`/load-model` 管理训练权重。
- **共同命令**：`/help` 显示中文操作说明，`/show` 显示中文状态，`/back` 返回主菜单，`/quit` 退出。不同引擎不能混用训练权重。
- **参数提示**：例如输入 `/device a`，Tab 会补全成 `/device auto`，候选旁显示“自动检测训练设备”；`on`/`off`、`speech`/`singing` 也附带中文解释。上下方向键查看历史命令，带空格的文件路径请使用双引号。

### 无界面批处理

命令行也必须先选择子菜单，再传入参数，最后显式写 `/convert` 或 `/train`：

```powershell
# RVC 训练
.\build\vs2022\Release\myvoice_cli.exe `
  /RVC /python "D:\myVoice\.venv-rvc\Scripts\python.exe" `
  /dataset "D:\voice_data\male" /train-name male_v1 /epochs 200 /batch 4 /train

# Seed-VC 文件转换
.\build\vs2022\Release\myvoice_cli.exe `
  /Seed-VC /python "D:\myVoice\.venv-seed\Scripts\python.exe" `
  /source "D:\audio\female.wav" /target "D:\audio\male.wav" `
  /output "D:\audio\result.wav" /convert
```

脚本不会自动运行转换：`/convert` 或 `/train` 必须位于最后一个参数；否则只设置并显示选项。`/help` 显示当前菜单帮助。例如 `myvoice_cli /RVC /help`。失败时输出非零退出码。

## 六、C++ API 及结构

```cpp
#include <myvoice/VoiceTrainer.h>
#include <myvoice/VoiceConverter.h>

myvoice::TrainingConfiguration train_config;
train_config.rvc_root = "D:/RVC";
train_config.python = "D:/RVC/.venv/Scripts/python.exe";
myvoice::TrainingRequest train;
train.dataset = "D:/voice_data/male";
train.name = "male_v1";
train.epochs = 200;
auto training = myvoice::VoiceTrainer(train_config).train(train);

myvoice::Configuration cfg;
cfg.python = train_config.python;
cfg.rvc_root = train_config.rvc_root;
myvoice::Request req;
req.mode = myvoice::Mode::Rvc;
req.trained_model = "D:/RVC/assets/weights/male_v1.pth";
req.source = "D:/audio/female.wav";
req.output = "D:/audio/result.wav";
auto output = myvoice::VoiceConverter(cfg).convert(req);
```

关键目录：`apps/` TUI/CLI；`src/` 进程与推理/训练调度；`include/myvoice/` 公共 C++ API；`scripts/rvc_train.py` 官方 RVC 训练桥接；`tests/` 模拟推理与训练测试；`third_party/ftxui/` 固定版本源码和 MIT 许可证。

## 安全与许可证

仓库自身许可证见 `LICENSE`；FTXUI 原许可证见 `third_party/ftxui/LICENSE`。RVC、Seed-VC、模型权重、数据集和第三方预训练模型遵循各自许可证；用户应拥有目标声音的合法使用授权。此程序不提供或下载未经授权的真人声音数据。

### Python 环境示例（Windows）

在仓库根目录执行（需要预先安装对应版本的 Python；按 GPU/CUDA 环境选用正确的 PyTorch 依赖）：

```powershell
# Seed-VC 推荐 Python 3.10（源码在本地）
py -3.10 -m venv .venv-seed
.\.venv-seed\Scripts\python.exe -m pip install -r third_party/seed-vc/requirements.txt

# RVC 当前仓库提供 Python 3.12 的依赖清单，例如 CPU 版本
py -3.12 -m venv .venv-rvc
.\.venv-rvc\Scripts\python.exe -m pip install -r third_party/rvc/requirments_cpu_py312.txt
```

GPU 环境要选择合适 CUDA/torch 组合与 RVC `requirments_cu118_py312.txt` 或 `requirments_cu128_py312.txt`，不可混装。RVC 训练还需 `assets/pretrained_v2/` 等模型资源；Seed-VC 初次推理也可能通过 Hugging Face 下载对应权重。本项目只内置引擎源码，**不会自动配置 Python 或下载全部模型权重**。 

## 内置引擎来源与离线依赖

详见 [third_party/ENGINES.md](third_party/ENGINES.md)。默认 Python 可执行文件仍是 `python`（Windows）或 `python3`（Linux）；不同引擎若装在不同 Python 虚拟环境，可进入对应子菜单后使用 `/python <可执行文件>` 覆盖自动选择。

**迁移提示：** 老版本的 `/rvc-root` / `/seed-root` 配置仍有效，显式指定时优先于内置路径。现在不设置它们也可以调用源码，但启动训练或转换前依然必须配置对应 Python 依赖、模型权重和授权音频数据。
