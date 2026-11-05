# myVoice Portable Python Runtime

本文件可由 Git 正常跟踪。**完整解释器和 site-packages 以 GitHub Release 附件形式提供**，对应压缩包中的文件在提取后直接放在本目录。

| 平台 | 引擎 | Python | 解压后路径 |
| --- | --- | --- | --- |
| Windows x64 | Seed-VC | 3.10 | `runtime/windows-x64/seed-vc/python.exe` |
| Windows x64 | RVC | 3.12 | `runtime/windows-x64/rvc/python.exe` |
| Linux x86-64 | Seed-VC | 3.10 | `runtime/linux-x64/seed-vc/bin/python3` |
| Linux x86-64 | RVC | 3.12 | `runtime/linux-x64/rvc/bin/python3` |

下载：`scripts/get_portable_python.ps1` 或 `scripts/get_portable_python.sh`。ZIP 附件在仓库 Releases 的 [portable-python-v1](https://github.com/a289672082/myVoice/releases/tag/portable-python-v1) 条目中。

环境构建脚本：`scripts/build_portable_python.py`。它在 GitHub Actions 的 Windows/Linux 运行器上借助 uv 下载自包含 CPython、安装对应 Python 包，随后对打包后另一路径进行可执行测试。**构建工具只在 CI 上使用，使用者无须安装 Python 或 uv。**

注意：CPU 包不等于 CUDA GPU 包；声音转换模型权重也不属于 Python 运行环境。RVC / Seed-VC 的模型按各自许可证单独下载，商用与再分发请核对许可证。
