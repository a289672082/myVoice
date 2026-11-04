# 内置语音引擎源码

此处是可构建的独立源码快照，**不是** Git 子模块，也不通过 CMake FetchContent 下载。myVoice C++ 只启动这些目录中的 Python 脚本。

- **RVC**：`third_party/rvc/`，来源 `RVC-Project/Retrieval-based-Voice-Conversion-WebUI`，固定提交 `81eed5e8f68b6bed1789f682fe78cdd324495afc`，MIT 许可证：`rvc/LICENSE`。全部原仓库工作树内容中移除了 `.git` 和只用于子模块配置的 `.gitmodules`（不会自动拉取子模块）。
- **Seed-VC**：`third_party/seed-vc/`，来源 `Plachtaa/seed-vc`，固定提交 `51383efd921027683c89e5348211d93ff12ac2a8`，GPL-3.0 许可证：`seed-vc/LICENSE`。保留推理、训练、配置、模块与说明；排除不属于源码的样例人声/歌曲、展示视频、部分评估 ONNX 模型及大型 `campplus_cn_common.bin` 权重。

仓库内源码、配置和训练器可离线读取/编译，不代表依赖或权重可离线获得；RVC 和 Seed-VC 仍需要 Python 环境、第三方包、预训练模型（部分模型会通过 Hugging Face 下载）及合规训练数据。

**许可证注意：** myVoice 主工程的许可不替代内置第三方代码的许可证。Seed-VC 为 GPL-3.0；重新分发或集成时请确保满足对应源码提供、版权声明及其他适用义务。预训练模型权重及训练数据的授权也要单独核对。
