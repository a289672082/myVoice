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

## GPU 自动配置（可选）

先下载上表的 CPU 便携 Python，再启动 `myvoice_cli.exe`，进入 `/RVC` 或 `/Seed-VC` 子菜单：

```text
/gpu-check
/gpu-setup
/train-wizard
```

`/gpu-check` 不会下载文件；`/gpu-setup` 才会从官方资源下载指定版本 CUDA 12.1 PyTorch，完整复制 CPU 环境到单独目录，真实 GPU 计算验证成功后才启用：

```text
runtime/
  windows-x64/
    rvc/              # 保留原 CPU 环境
    seed-vc/          # 保留原 CPU 环境
    rvc-gpu/          # 运行 /gpu-setup 后生成
    seed-vc-gpu/      # 运行 /gpu-setup 后生成
  linux-x64/
    rvc/
    seed-vc/
    rvc-gpu/
    seed-vc-gpu/
```

这些 GPU 目录 **不会由普通 git clone 自动生成**；只有在本机下载并通过 CUDA 验证后才创建。若驱动太旧、显卡型号不在固定 PyTorch 2.4.x 验证范围内，安装器会拒绝继续并保留 CPU 环境。原有 `/python` 显式自定义解释器设置仍然优先。
