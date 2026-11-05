#!/usr/bin/env python3
"""myVoice 智能训练：资源预检、GPU 自适应、音频质量门禁、模型登记。

RVC 调用仓库已有 rvc_train.py；Seed-VC V2 使用其 train_v2.py 正式微调入口。
只使用目标人物授权音频。自动过滤是启发式质量筛查，不等于声音分离/人工审核。
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

NAME_RE = re.compile(r"[A-Za-z0-9_-]{1,64}\Z")
EXTENSIONS = {".wav", ".flac", ".mp3", ".ogg", ".m4a", ".opus"}
REPO = Path(__file__).resolve().parents[1]

# Hugging Face 的官方项目文件路径，非用户可配置的任意 URL。
RVC_ASSETS = [
    ("hubert_base/config.json", "hubert_base/config.json", 32),
    ("hubert_base/preprocessor_config.json", "hubert_base/preprocessor_config.json", 32),
    ("hubert_base/pytorch_model.bin", "hubert_base/pytorch_model.bin", 1_000_000),
    ("rmvpe.pt", "rmvpe/rmvpe.pt", 1_000_000),
    ("pretrained_v2/f0G48k.pth", "pretrained_v2/f0G48k.pth", 1_000_000),
    ("pretrained_v2/f0D48k.pth", "pretrained_v2/f0D48k.pth", 1_000_000),
]
SEED_ASSETS = [
    ("Plachta/Seed-VC", "v2/cfm_small.pth", "cfm_small.pth", 1_000_000),
    ("Plachta/Seed-VC", "v2/ar_base.pth", "ar_base.pth", 1_000_000),
    ("Plachta/ASTRAL-quantization", "bsq32/bsq32_light.pth", "bsq32_light.pth", 1_000_000),
    ("Plachta/ASTRAL-quantization", "bsq2048/bsq2048_light.pth", "bsq2048_light.pth", 1_000_000),
    ("funasr/campplus", "campplus_cn_common.bin", "campplus_cn_common.bin", 1_000_000),
]


def atomic_copy(source: Path, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix=".myvoice-", dir=dest.parent, delete=False) as tmp:
        partial = Path(tmp.name)
        try:
            with Path(source).open("rb") as reader:
                shutil.copyfileobj(reader, tmp, length=2 * 1024 * 1024)
            tmp.flush()
            os.fsync(tmp.fileno())
        except BaseException:
            partial.unlink(missing_ok=True)
            raise
    try:
        os.replace(partial, dest)
    finally:
        partial.unlink(missing_ok=True)


def required_asset(repo: str, filename: str, dest: Path, minimum: int,
                   *, offline: bool = False, downloader=None) -> Path:
    """若已存在则检查大小；否则通过 HF 校验缓存下载后原子复制，避免留下部分文件。"""
    if dest.is_file() and dest.stat().st_size >= minimum:
        return dest.resolve()
    if offline:
        raise FileNotFoundError(f"缺少预训练资源 {dest}；取消 /offline 或先联网下载官方权重")
    if downloader is None:
        from huggingface_hub import hf_hub_download
        downloader = hf_hub_download
    revision = os.environ.get("MYVOICE_HF_REVISION") or "main"
    print(f"[资源] 官方仓库 {repo}/{filename} ({revision})", flush=True)
    cached = Path(downloader(repo_id=repo, filename=filename, revision=revision))
    if not cached.is_file() or cached.stat().st_size < minimum:
        raise RuntimeError(f"下载的权重异常或不完整：{repo}/{filename}")
    atomic_copy(cached, dest)
    if dest.stat().st_size < minimum:
        raise RuntimeError(f"保存权重失败：{dest}")
    return dest.resolve()


def ensure_pretrained(engine: str, root: Path, *, offline: bool = False, downloader=None) -> dict[str, str]:
    assets = {}
    if engine == "rvc":
        for remote, local, minimum in RVC_ASSETS:
            assets[local] = str(required_asset(
                "lj1995/VoiceConversionWebUI", remote, root / "assets" / local,
                minimum, offline=offline, downloader=downloader))
    elif engine == "seed-vc":
        # 与内置 vc_wrapper.py 的 DEFAULT_*_CHECKPOINT 常量完全一致。
        local_dir = root / "checkpoints" / "myvoice_pretrained"
        for repo, remote, filename, minimum in SEED_ASSETS:
            assets[filename] = str(required_asset(
                repo, remote, local_dir / filename,
                minimum, offline=offline, downloader=downloader))
        # BigVGAN、Whisper 等组件由官方 wrapper 的 from_pretrained 首次按需缓存；
        # 已在此处预取训练所需的 2 个关键微调初始化权重及 3 个编码器检查点。
    else:
        raise ValueError(f"未知引擎：{engine}")
    return assets


def select_hardware(engine: str, requested: str = "auto", batch: int = 0,
                    preferred_gpu: int = -1, *, torch_module=None) -> dict:
    """优先使用可由当前 PyTorch 真正访问的 CUDA，而不是仅看 nvidia-smi 是否存在。"""
    if requested not in {"auto", "cpu", "cuda"}:
        raise ValueError("设备必须是 auto/cpu/cuda")
    if batch < 0 or batch > 128:
        raise ValueError("batch 需为 auto(0) 或 1..128")
    if torch_module is None:
        import torch
        torch_module = torch
    cuda = torch_module.cuda
    found = cuda.is_available() and cuda.device_count() > 0
    if requested == "cuda" and not found:
        raise RuntimeError("当前便携 PyTorch 不支持 CUDA。已下载的是 CPU 版；"
                           "请准备匹配 NVIDIA/CUDA 的 PyTorch 运行环境，或使用 /device auto")
    if requested == "cpu" or not found:
        if requested == "auto":
            print("[硬件] 未找到 PyTorch 可用的 CUDA，退回 CPU（训练可能非常慢）", flush=True)
        return {"device": "cpu", "gpu": 0, "vram_gb": 0.0,
                "batch": batch or 1, "workers": 0 if os.name == "nt" else 2}
    gpu = preferred_gpu
    if gpu < 0:
        gpu = max(range(cuda.device_count()),
                  key=lambda i: cuda.get_device_properties(i).total_memory)
    if not 0 <= gpu < cuda.device_count():
        raise ValueError(f"GPU {gpu} 不存在，可用范围 0..{cuda.device_count()-1}")
    props = cuda.get_device_properties(gpu)
    vram = props.total_memory / (1024**3)
    if engine == "seed-vc":
        chosen = 2 if vram >= 16 else 1
    else:
        chosen = 8 if vram >= 16 else 4 if vram >= 10 else 2 if vram >= 6 else 1
    plan = {"device": "cuda", "gpu": gpu, "vram_gb": round(vram, 2),
            "name": props.name, "batch": batch or chosen,
            "workers": 0 if os.name == "nt" else 2}
    print(f"[硬件] {props.name}, 显存 {vram:.1f} GiB, batch={plan['batch']}", flush=True)
    return plan


def read_audio(path: Path):
    import numpy as np
    import soundfile as sf
    try:
        audio, sr = sf.read(path, dtype="float32", always_2d=True)
    except (RuntimeError, ValueError):
        import librosa
        audio, sr = librosa.load(str(path), sr=None, mono=False)
        audio = np.asarray(audio, dtype=np.float32)
        if audio.ndim == 1:
            audio = audio[:, None]
        else:
            audio = audio.T
    if audio.shape[0] == 0 or sr < 8000:
        raise ValueError("空文件或采样率低于 8 kHz")
    # 对于立体声，保守平均声道，保留音量峰值判断。
    mono = audio.mean(axis=1)
    return mono, sr


def check_audio(audio, sample_rate: int, engine: str) -> tuple[list[str], list[str], dict]:
    """声音质量启发式检查。高背景噪声等只能发警告，避免误伤持续歌声。"""
    import numpy as np
    x = np.asarray(audio, dtype=np.float32)
    duration = len(x) / sample_rate
    rejected, warnings = [], []
    upper = 30.0 if engine == "seed-vc" else 180.0
    if duration < 1 or duration > upper:
        rejected.append(f"时长 {duration:.1f}s 不在 1..{int(upper)}s")
    if not np.all(np.isfinite(x)):
        rejected.append("存在 NaN/Inf")
        x = np.nan_to_num(x)
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    rms = float(np.sqrt(np.mean(np.square(x, dtype=np.float64)))) if len(x) else 0.0
    clipping = float(np.mean(np.abs(x) >= 0.995)) if len(x) else 0.0
    dc = float(abs(np.mean(x, dtype=np.float64))) if len(x) else 0.0
    rms_db = 20 * math.log10(max(rms, 1e-9))
    if peak < 0.02 or rms_db < -42:
        rejected.append(f"声音过小或近乎静音 ({rms_db:.1f} dBFS)")
    if clipping > 0.005:
        rejected.append(f"严重削波 ({clipping*100:.2f}% 样本触顶)")
    if dc > 0.12:
        rejected.append(f"明显直流偏移 ({dc:.2f})")
    # 基于帧能量的噪声代理指标：只发出可能持续背景声的警告，不自动宣称 SNR。
    if len(x) > sample_rate * 2:
        hop = max(1, sample_rate // 4)
        frames = x[:(len(x) // hop) * hop].reshape(-1, hop)
        levels = np.sqrt(np.mean(frames.astype(np.float64) ** 2, axis=1))
        quiet = float(np.percentile(levels, 15))
        loud = float(np.percentile(levels, 85))
        contrast = 20 * math.log10((loud + 1e-6) / (quiet + 1e-6))
        if quiet > 0.05 and contrast < 6:
            warnings.append("持续背景声/伴奏风险（能量对比低），建议人工复核")
    if peak > 0.98 and clipping <= 0.005:
        warnings.append("峰值接近满幅，请检查是否压限过度")
    return rejected, warnings, {
        "duration_s": round(duration, 3), "rms_dbfs": round(rms_db, 2),
        "peak": round(peak, 5), "clip_fraction": round(clipping, 6)}


def prepare_dataset(source: Path, dest: Path, engine: str, *,
                    min_seconds: float = 5.0) -> dict:
    """筛选后标准化为 WAV，保存独立训练目录和可审计报告，不修改原音频。"""
    import soundfile as sf
    source = source.resolve(strict=True)
    if not source.is_dir():
        raise NotADirectoryError(str(source))
    dest = dest.resolve()
    if source == dest or source in dest.parents or dest in source.parents:
        raise ValueError("训练素材目录不能和筛选结果目录相同或互相包含")
    candidates = sorted(p for p in source.rglob("*") if p.is_file() and
                        p.suffix.lower() in EXTENSIONS and not p.is_symlink())
    if not candidates:
        raise RuntimeError("训练目录没有可识别的音频文件")
    dest.mkdir(parents=True, exist_ok=True)
    for old in dest.glob("*.wav"):
        old.unlink()
    accepted = []
    rejected = []
    total = 0.0
    for i, path in enumerate(candidates, 1):
        try:
            x, rate = read_audio(path)
            reasons, warnings, metrics = check_audio(x, rate, engine)
            entry = {"source": str(path), "warnings": warnings, **metrics}
            if reasons:
                entry["reasons"] = reasons
                rejected.append(entry)
                continue
            output = dest / f"{i:05d}.wav"
            sf.write(output, x, rate, subtype="PCM_16")
            entry["prepared"] = str(output)
            accepted.append(entry)
            total += metrics["duration_s"]
        except Exception as exc:
            rejected.append({"source": str(path), "reasons": [str(exc)]})
    report = {"engine": engine, "source_dir": str(source), "accepted": accepted,
              "rejected": rejected, "duration_s": round(total, 2),
              "note": "启发式质量门禁；噪声、伴奏、错误说话人需要人工审核。"}
    report_path = dest.parent / "myvoice_quality_report.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2),
                           encoding="utf-8")
    print(f"[质检] 合格 {len(accepted)} / {len(candidates)}，总 {total:.1f}s，"
          f"报告：{report_path}", flush=True)
    if not accepted or total < min_seconds:
        raise RuntimeError("可用音频不足，请检查质检报告并补充干净录音")
    return report


def register_model(index_path: Path, engine: str, name: str,
                   primary: Path, secondary: Path | None = None) -> Path:
    """index.tsv 格式：engine TAB name TAB primary TAB secondary；仅登记真实完成的文件。"""
    if engine not in {"rvc", "seed-vc"} or not NAME_RE.fullmatch(name):
        raise ValueError("非法模型名称")
    primary = primary.resolve(strict=True)
    if not primary.is_file() or primary.stat().st_size < 32:
        raise ValueError(f"模型权重无效：{primary}")
    secondary = secondary.resolve(strict=True) if secondary else None
    if secondary and (not secondary.is_file() or secondary.stat().st_size < 32):
        raise ValueError(f"辅助权重无效：{secondary}")
    fields = [engine, name, str(primary), str(secondary or "")]
    if any("\t" in x or "\n" in x or "\r" in x for x in fields):
        raise ValueError("登记字段包含非法制表符或换行")
    index_path.parent.mkdir(parents=True, exist_ok=True)
    records: dict[tuple[str, str], list[str]] = {}
    if index_path.exists():
        for row in csv.reader(index_path.open("r", encoding="utf-8"), delimiter="\t"):
            if len(row) == 4 and row[0] in {"rvc", "seed-vc"}:
                records[(row[0], row[1])] = row
    records[(engine, name)] = fields
    temp = index_path.with_name(index_path.name + f".{os.getpid()}.part")
    try:
        with temp.open("w", encoding="utf-8", newline="") as out:
            writer = csv.writer(out, delimiter="\t", lineterminator="\n")
            writer.writerows(records[k] for k in sorted(records))
        os.replace(temp, index_path)
    finally:
        temp.unlink(missing_ok=True)
    print(f"[注册] {engine}/{name}：{primary}", flush=True)
    return index_path


def stage(command: list[str], cwd: Path, env=None) -> None:
    print("[执行] " + " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True)


def execute(args: argparse.Namespace) -> None:
    if not NAME_RE.fullmatch(args.name):
        raise ValueError("模型名只能使用英文字母、数字、下划线或连字符（1..64 字符）")
    root = Path(args.root).resolve(strict=True)
    if args.engine == "rvc" and not (root / "train/train.py").is_file():
        raise FileNotFoundError("缺少内置 RVC train/train.py")
    if args.engine == "seed-vc" and not (root / "train_v2.py").is_file():
        raise FileNotFoundError("缺少内置 Seed-VC train_v2.py")
    # 资源下载在检测数据质量之后：不浪费带宽在空/错误音频集上。
    folder = root / ("logs" if args.engine == "rvc" else "runs") / args.name
    cleaned = folder / "myvoice_clean_audio"
    report = prepare_dataset(Path(args.dataset), cleaned, args.engine,
                             min_seconds=args.minimum_seconds)
    plan = select_hardware(args.engine, args.device, args.batch, args.gpu)
    (folder / "myvoice_training_plan.json").write_text(
        json.dumps({"engine": args.engine, "name": args.name, "device": plan,
                    "quality_duration_s": report["duration_s"]}, ensure_ascii=False, indent=2),
        encoding="utf-8")
    if args.dry_run:
        print("[检查] dry-run 通过，未下载权重，也未开始训练", flush=True)
        return
    assets = ensure_pretrained(args.engine, root, offline=args.offline)
    if args.engine == "rvc":
        cmd = [
            sys.executable, str(REPO / "scripts" / "rvc_train.py"),
            "--rvc-root", str(root), "--dataset", str(cleaned),
            "--name", args.name, "--epochs", str(args.epochs),
            "--batch", str(plan["batch"]), "--save-every", str(min(args.save_every, args.epochs)),
            "--workers", str(args.workers or plan["workers"]),
            "--device", plan["device"], "--gpu", str(plan["gpu"]),
            "--index", args.index
        ]
        stage(cmd, root)
        model = root / "assets" / "weights" / (args.name + ".pth")
        secondary = None
    else:
        # train_v2.py 输出的 CFM/AR 检查点符合 inference_v2.py 的 load_checkpoints 接口。
        # 默认训练 CFM，AR 沿用官方预训练权重，避免无必要地训练大模型。
        cmd = [
            sys.executable, "-m", "accelerate.commands.launch",
            "--num_processes", "1", "--mixed_precision", "no",
            "train_v2.py", "--config", "configs/v2/vc_wrapper.yaml",
            "--dataset-dir", str(cleaned), "--run-name", args.name,
            "--batch-size", str(plan["batch"]), "--max-steps", str(args.steps),
            "--max-epochs", str(args.epochs), "--save-every", str(min(args.save_every, args.steps)),
            "--num-workers", str(args.workers if args.workers else plan["workers"]),
            "--pretrained-cfm-ckpt", assets["cfm_small.pth"],
            "--pretrained-ar-ckpt", assets["ar_base.pth"],
            "--train-cfm"
        ]
        env = os.environ.copy()
        env["HF_HUB_CACHE"] = str(root / "checkpoints" / "hf_cache")
        # 官方 wrapper 还会按需下载模型组件（Whisper/BigVGAN 等）。
        if plan["device"] == "cpu":
            env["CUDA_VISIBLE_DEVICES"] = ""
            cmd[3:3] = ["--cpu"]
        else:
            env["CUDA_VISIBLE_DEVICES"] = str(plan["gpu"])
        stage(cmd, root, env=env)
        candidates = list(folder.glob("CFM_epoch_*_step_*.pth"))
        if not candidates:
            raise RuntimeError("Seed-VC 微调结束，但未找到 CFM 检查点；请检查训练日志")
        model = max(candidates, key=lambda p: (p.stat().st_mtime_ns, p.name))
        secondary = Path(assets["ar_base.pth"])
    register_model(Path(args.registry), args.engine, args.name, model, secondary)
    print(f"[完成] 已自动注册模型 {args.engine}/{args.name}", flush=True)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="myVoice 智能训练")
    parser.add_argument("--engine", choices=["rvc", "seed-vc"], required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--dataset", required=True)
    parser.add_argument("--registry", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--device", choices=["auto", "cuda", "cpu"], default="auto")
    parser.add_argument("--gpu", type=int, default=-1)
    parser.add_argument("--batch", type=int, default=0)
    parser.add_argument("--epochs", type=int, default=200)
    parser.add_argument("--steps", type=int, default=1000)
    parser.add_argument("--save-every", type=int, default=200)
    parser.add_argument("--workers", type=int, default=0)
    parser.add_argument("--index", choices=["on", "off"], default="on")
    parser.add_argument("--minimum-seconds", type=float, default=5.0)
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    if args.epochs < 1 or args.steps < 1 or args.save_every < 1 or args.workers < 0:
        parser.error("epochs/steps/save-every 必须 >0，workers 必须 >=0")
    return args


if __name__ == "__main__":
    try:
        execute(parse_args())
    except Exception as exc:
        print(f"[错误] {type(exc).__name__}: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
