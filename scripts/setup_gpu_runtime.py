#!/usr/bin/env python3
"""新手 GPU 环境准备：将 CPU 便携 Python 克隆到独立 -gpu 目录，验证后才启用。

绝不覆盖 CPU 运行时、系统 Python 或 NVIDIA 驱动。
网络下载使用固定版本的官方 uv 二进制及官方 SHA256 校验文件；
PyTorch 从对应的官方 CUDA 12.1 索引获取。真实 GPU 验证必须在用户电脑执行。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

for _stream in (sys.stdout, sys.stderr):
    if _stream is not None and hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="backslashreplace")

ROOT = Path(__file__).resolve().parents[1]
UV_VERSION = "0.9.11"
TORCH = {"rvc": ("2.4.1", "0.19.1"), "seed-vc": ("2.4.0", "0.19.0")}
PLATFORM = "windows-x64" if sys.platform == "win32" else "linux-x64"
BASE_URL = f"https://github.com/astral-sh/uv/releases/download/{UV_VERSION}/"
ARCHIVE = ("uv-x86_64-pc-windows-msvc.zip" if sys.platform == "win32"
           else "uv-x86_64-unknown-linux-gnu.tar.gz")


def python_binary(directory: Path) -> Path:
    return directory / ("python.exe" if sys.platform == "win32" else "bin/python3")


def detect_nvidia(runner=subprocess.run) -> dict:
    """查询真正安装的 NVIDIA 驱动，不把发现显卡等同于 CUDA PyTorch 可用。"""
    if sys.platform not in {"win32", "linux"}:
        raise RuntimeError("当前仅支持 Windows x64 与 Linux x86-64 NVIDIA 显卡")
    try:
        result = runner(["nvidia-smi", "--query-gpu=name,compute_cap,driver_version",
                         "--format=csv,noheader"], capture_output=True, text=True,
                        timeout=15, check=True)
    except (OSError, subprocess.SubprocessError) as exc:
        raise RuntimeError("没有检测到可用的 NVIDIA 驱动/nvidia-smi；CPU 环境保持不变") from exc
    first = result.stdout.strip().splitlines()[0] if result.stdout.strip() else ""
    parts = [x.strip() for x in first.split(",")]
    if len(parts) < 3:
        raise RuntimeError("无法从 nvidia-smi 读取显卡型号、计算能力和驱动版本")
    name, cap, driver = parts[:3]
    try:
        major = int(cap.split(".")[0])
        minor = int(cap.split(".")[1])
        drv = tuple(int(x) for x in driver.split(".")[:2])
    except (ValueError, IndexError) as exc:
        raise RuntimeError("nvidia-smi 的驱动版本/计算能力无法解析") from exc
    if major < 5:
        raise RuntimeError(f"显卡 {name} 计算能力太旧，不支持本项目的 PyTorch CUDA 环境")
    if major >= 12:
        raise RuntimeError(f"{name} 的计算能力 {cap} 超出项目固定 PyTorch 2.4.x 的验证范围，"
                           "不能冒险安装不兼容的 CUDA 版本；请使用更新的受支持运行环境")
    # 官方 CUDA 12.1 对应 Windows ≥ 531.14 / Linux ≥ 530.30。
    min_driver = (531, 14) if sys.platform == "win32" else (530, 30)
    if drv < min_driver:
        raise RuntimeError(f"NVIDIA 驱动 {driver} 过旧，CUDA 12.1 至少需要 "
                           f"{min_driver[0]}.{min_driver[1]}；请先从 NVIDIA 官方更新驱动")
    return {"name": name, "compute_capability": cap, "driver": driver, "backend": "cu121"}


def fetch_verified_uv(output: Path, *, urlopen=urllib.request.urlopen) -> Path:
    """仅下载固定 uv 可执行程序，不执行在线脚本；使用官方同名 SHA256 校验。"""
    output.mkdir(parents=True, exist_ok=True)
    url = BASE_URL + ARCHIVE
    with urlopen(url + ".sha256", timeout=45) as response:
        expected = response.read(256).decode("ascii").strip().split()[0].lower()
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise RuntimeError("官方 uv SHA256 清单格式异常")
    zipped = output / ARCHIVE
    h = hashlib.sha256()
    with urlopen(url, timeout=60) as response, zipped.open("wb") as target:
        total = 0
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            total += len(chunk)
            if total > 70 * 1024 * 1024:
                raise RuntimeError("uv 二进制包异常过大")
            h.update(chunk)
            target.write(chunk)
    if h.hexdigest() != expected:
        raise RuntimeError("官方 uv 压缩包的 SHA256 校验失败")
    tool = output / ("uv.exe" if sys.platform == "win32" else "uv")
    if sys.platform == "win32":
        with zipfile.ZipFile(zipped) as archive:
            matches = [n for n in archive.namelist() if n.split("/")[-1] == "uv.exe"]
            if len(matches) != 1:
                raise RuntimeError("uv Windows 包结构异常")
            with archive.open(matches[0]) as src, tool.open("wb") as dst:
                shutil.copyfileobj(src, dst)
    else:
        with tarfile.open(zipped, "r:gz") as archive:
            matches = [m for m in archive.getmembers() if m.isfile() and
                       m.name.split("/")[-1] == "uv"]
            if len(matches) != 1:
                raise RuntimeError("uv Linux 包结构异常")
            with archive.extractfile(matches[0]) as src, tool.open("wb") as dst:
                shutil.copyfileobj(src, dst)
        tool.chmod(0o755)
    return tool


def plan(engine: str, gpu: dict) -> dict:
    if engine not in TORCH:
        raise ValueError("引擎只能是 rvc 或 seed-vc")
    version, vision = TORCH[engine]
    return {"engine": engine, "platform": PLATFORM, "gpu": gpu,
            "torch": version, "torchvision": vision, "torchaudio": version}


def prepare(engine: str, *, dry_run: bool = False, uv_path: str | None = None) -> Path:
    if engine not in TORCH:
        raise ValueError("不支持的引擎")
    gpu = detect_nvidia()
    src = ROOT / "runtime" / PLATFORM / engine
    dest = ROOT / "runtime" / PLATFORM / f"{engine}-gpu"
    print("[GPU] " + json.dumps(gpu, ensure_ascii=False), flush=True)
    print(f"[GPU] 独立环境：{dest}（CPU 原环境不会被修改）", flush=True)
    print("[GPU] 将从官方 PyTorch CUDA 12.1 索引下载多个大型二进制包", flush=True)
    if dry_run:
        print(json.dumps(plan(engine, gpu), ensure_ascii=False, indent=2), flush=True)
        return dest
    if not python_binary(src).is_file():
        raise FileNotFoundError("缺少 CPU 便携 Python；请先运行 scripts/get_portable_python")
    if dest.exists():
        ready = dest / "myvoice-gpu-ready.json"
        if ready.is_file() and python_binary(dest).is_file():
            print(f"[GPU] 环境已存在：{dest}，不会重复下载安装。", flush=True)
            return dest
        raise RuntimeError(f"GPU 目标目录已存在但未验证：{dest}，请检查后手动移走该目录")
    with tempfile.TemporaryDirectory(prefix="myvoice-uv-") as tmp:
        tool = Path(uv_path).resolve() if uv_path else fetch_verified_uv(Path(tmp))
        if not tool.is_file():
            raise FileNotFoundError(f"缺少 uv：{tool}")
        staging = dest.parent / (f".{dest.name}.{os.getpid()}.building")
        if staging.exists():
            raise RuntimeError(f"临时目录已存在：{staging}")
        try:
            print("[GPU] 复制 CPU 环境到独立临时目录...", flush=True)
            shutil.copytree(src, staging, symlinks=True)
            py = python_binary(staging)
            # 如果解压包里含绝对符号链接，绝不能让 uv 意外修改原 CPU 环境。
            resolved_py = py.resolve()
            if resolved_py == python_binary(src).resolve() or src.resolve() in resolved_py.parents:
                raise RuntimeError("便携 Python 符号链接指向 CPU 原目录，拒绝修改")
            torch, vision = TORCH[engine]
            print("[GPU] 正在安装匹配的 CUDA 版 PyTorch（可能占用数 GB）...", flush=True)
            subprocess.run([
                str(tool), "pip", "install", "--python", str(py),
                "--break-system-packages", "--torch-backend", "cu121",
                "--reinstall-package", "torch", "--reinstall-package", "torchvision",
                "--reinstall-package", "torchaudio",
                f"torch=={torch}", f"torchvision=={vision}", f"torchaudio=={torch}"
            ], check=True)
            print("[GPU] 进行真实 CUDA 推理与显存分配验证...", flush=True)
            subprocess.run([
                str(py), "-c",
                "import torch,torchaudio,torchvision;"
                "assert torch.cuda.is_available(), 'CUDA PyTorch 不可用';"
                "x=torch.ones((64,64),device='cuda');"
                "assert float((x@x).sum().item()) > 0;"
                "print(torch.__version__, torch.cuda.get_device_name(0))"
            ], check=True)
            (staging / "myvoice-gpu-ready.json").write_text(
                json.dumps(plan(engine, gpu), ensure_ascii=False, indent=2), encoding="utf-8")
            staging.rename(dest)
            print("[GPU] 已验证并启用：" + str(dest), flush=True)
            return dest
        finally:
            if staging.exists():
                shutil.rmtree(staging, ignore_errors=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description="myVoice 自动配置独立 GPU 运行时")
    parser.add_argument("--engine", choices=list(TORCH), required=True)
    parser.add_argument("--dry-run", action="store_true", help="仅检查硬件，不下载")
    parser.add_argument("--uv", default=None, help="可选：使用事先下载并信任的 uv 可执行程序")
    args = parser.parse_args(argv)
    return prepare(args.engine, dry_run=args.dry_run, uv_path=args.uv)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("[GPU] 配置失败（CPU 环境未被修改）：" + str(e), file=sys.stderr)
        raise SystemExit(1)
