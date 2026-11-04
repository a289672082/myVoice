# myVoice

Windows / Linux 跨平台 **C++17 + FTXUI** 语音转换与 **RVC V2 目标音色训练** 项目。可在 **Visual Studio 2022** 中使用 CMake 工程（自动生成 `.sln`）。命令统一以 `/` 开头，支持实时命令提示、Tab 补全、文件路径补全和历史命令。

> **FTXUI v7.0.3 全部所需源码已内置到 `third_party/ftxui/`**（MIT，保留原 LICENSE）。CMake **只从本地源码编译**，不使用 `FetchContent`、Git 子模块或 `find_package(ftxui)`，**配置和 C++ 构建无需联网**。AI 模型、PyTorch、CUDA、RVC/Seed-VC Python 仓库和权重不在此仓库内，需要单独安装。

## 功能

| 功能 | 当前实现 |
| --- | --- |
| FTXUI 控制台、`/` 命令、Tab 自动补全 | ✅ |
| Windows / VS2022、Linux 编译 | ✅ CMake |
| `/engine speech` | Seed-VC V2 零样本普通语音转换 |
| `/engine singing` | Seed-VC V1 带 F0 条件歌声转换（默认不移调） |
| `/train` | RVC V2 48k F0 单说话人训练、RMVPE、HuBERT、索引生成 |
| `/engine rvc` | 使用训练后的 RVC `.pth` 文件进行离线推理 |
| `/train-log` | 查看训练日志尾部 |
| 高音失真自动修复 / 自动音域映射 | **未实现**，不能保证消除女声高音转男声失真 |
| 实时麦克风、GUI 训练参数面板、纯 C++ 模型训练 | **未实现** |

训练流水线实际调用你独立安装的 RVC 官方 Python 训练脚本，C++ 负责参数校验、子进程调度、超时、日志和 FTXUI 操作。**未经真实 GPU、模型权重及语音样本验证最终音质**，仅已完成模拟训练/转换测试。

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

## 二、训练男声音色（RVC V2）

1. 另行安装 [RVC 官方源码](https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI) 的 Python 环境及依赖（推荐可用 CUDA GPU），取得 `assets/hubert_base/`、`assets/rmvpe/rmvpe.pt`、`assets/pretrained_v2/f0G48k.pth`、`f0D48k.pth` 等**官方**预训练权重。训练桥接脚本只适配仓库中具有 `train/preprocess.py`、`train/dataset/extract_f0.py`、`train/dataset/extract_hubert_feature.py`、`train/train.py`、`train/train_index.py` 的版本。
2. 把得到授权的目标男声录音（建议有正常讲话和真实高音/唱歌内容）放到独立目录，比如 `D:\voice_data\male`。尽量避免音乐伴奏、噪声、明显削波和 TTS 合成伪影。训练阶段使用的是**目标男声**，不是要转换的女声音频。
3. 打开 FTXUI，依次输入：

```text
/python "D:\RVC\.venv\Scripts\python.exe"
/rvc-root "D:\RVC"
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
/train
```

训练开始后状态栏显示 `TRAINING`，中途使用 `/train-log` 查看最近输出，完整日志位于 `D:\RVC\logs\male_v1\myvoice_train.log`。RVC 的模型权重通常在 `D:\RVC\assets\weights\`，相关训练中间产物、检查点和索引位于 `D:\RVC\logs\male_v1\`，索引可链接到 `assets/indices`。

- `epochs`：1..10000，实际训练效果需对照评估检查点；不是越大越好。
- `batch`：1..128，显存不足时可降低；`/device cpu` 可用于排查/测试，但训练极慢。
- `index off`：仅训练权重，不构建 FAISS 索引。
- 训练不会自动下载模型权重，也不会自动从 IndexTTS 生成素材；如果使用合成数据，先自行生成和筛选。**只有高音真实覆盖的合格训练素材才可能改善高音失真**。

**提示**：某些 RVC 版本的训练脚本/资源布局不同。本实现针对已核对的 `RVC-Project/Retrieval-based-Voice-Conversion-WebUI` 的 `train/*` 结构；不保证兼容所有老分支或第三方打包版。失败时可从 `/train-log` 找到阶段和底层错误。

## 三、使用训练好的 RVC 音色转换

训练完成并取得 `.pth` 文件后：

```text
/python "D:\RVC\.venv\Scripts\python.exe"
/rvc-root "D:\RVC"
/engine rvc
/model "D:\RVC\assets\weights\male_v1.pth"
/source "D:\audio\female.wav"
/output "D:\audio\result.wav"
/convert
```

RVC 推理默认 RMVPE、`pitch=0`、`index-rate=0`，**不要求手工操作移调滑条**，也不需要再输入参考音频；但这并非自动基频校正。高音出现失真时，需要针对训练数据和转换模型实测。`/overwrite on` 可以覆盖已经存在的输出文件（但禁止覆盖源文件）。

## 四、免训练的 Seed-VC 转换

按 [Seed-VC 官方仓库](https://github.com/Plachtaa/seed-vc) 安装相应 Python 环境和权重：

```text
/python "D:\Seed-VC\.venv\Scripts\python.exe"
/seed-root "D:\Seed-VC"
/source "D:\audio\female.wav"
/target "D:\audio\male_reference.wav"
/output "D:\audio\result.wav"
/engine speech
/convert
```

唱歌时用 `/engine singing`，官方 F0 条件模型默认不移调，避免影响原始伴奏调性。

## 五、`/` 命令和自动提示

输入 `/` 显示可用命令；输入 `/tra` 会列出 `/train`、`/train-name`、`/train-timeout`、`/train-log` 等候选。输入 `/engine r` 时提示 `/engine rvc`。输入 `/source D:/au` 会根据本地文件系统提示路径；**Tab** 补全、**Enter** 执行、**↑↓** 查阅历史。路径有空格时加引号；Windows 反斜杠不会被丢弃。

使用 `/help` 查看全部命令，`/show` 查看转换和训练参数，`/quit` 退出（有运行中任务时需先完成）。

### 无界面批处理

无需进入 TUI，直接把 `/` 参数传给可执行文件，并以 `/train` 或 `/convert` 作为动作：

```powershell
.\build\vs2022\Release\myvoice_cli.exe `
  /python "D:\RVC\.venv\Scripts\python.exe" `
  /rvc-root "D:\RVC" `
  /dataset "D:\voice_data\male" `
  /train-name male_v1 /epochs 200 /batch 4 /train
```

Linux 也可使用相同 `/` 命令。脚本执行失败会输出非零退出码，训练细节保存在日志中。

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
