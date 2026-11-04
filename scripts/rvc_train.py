#!/usr/bin/env python3
"""RVC v2 single-speaker F0 training pipeline orchestrator.

Uses the official RVC project's scripts in the user-provided RVC directory;
model code and checkpoint weights are intentionally not redistributed here.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path


def require_file(p: Path, explanation: str = "") -> Path:
    if not p.is_file():
        raise FileNotFoundError(f"Required file missing: {p}. {explanation}")
    return p


def stage(root: Path, *args: object) -> None:
    cmd = [sys.executable, *map(str, args)]
    print("[myVoice] RUN:", " ".join(repr(x) for x in cmd[1:]), flush=True)
    subprocess.run(cmd, cwd=root, check=True)


def build_filelist(root: Path, exp: Path) -> int:
    """Match RVC WebUI v2/F0 filelist format and require actual stage outputs."""
    gt = exp / "0_gt_wavs"
    features = exp / "3_feature768"
    f0 = exp / "2a_f0"
    f0nsf = exp / "2b-f0nsf"
    if any(not p.is_dir() for p in (gt, features, f0, f0nsf)):
        raise RuntimeError("RVC preprocess/F0/HuBERT stages did not create required directories")
    keys = sorted({x.stem for x in gt.glob("*.wav")})
    lines = []
    for name in keys:
        paths = (gt / f"{name}.wav", features / f"{name}.npy",
                 f0 / f"{name}.wav.npy", f0nsf / f"{name}.wav.npy")
        if all(p.is_file() and p.stat().st_size for p in paths):
            # RVC filelist is pipe-delimited, so reject ambiguous paths.
            if any("|" in str(p) or "\n" in str(p) for p in paths):
                raise ValueError("Training file path contains '|' or a newline")
            lines.append("|".join([*(str(p.resolve()).replace("\\", "\\\\") for p in paths), "0"]))
    if not lines:
        raise RuntimeError("No valid training clips after preprocessing and feature extraction")
    mute = root / "logs" / "mute"
    m = (mute / "0_gt_wavs" / "mute48k.wav", mute / "3_feature768" / "mute.npy",
         mute / "2a_f0" / "mute.wav.npy", mute / "2b-f0nsf" / "mute.wav.npy")
    if all(p.is_file() for p in m):
        lines.extend(["|".join([*(str(p.resolve()).replace("\\", "\\\\") for p in m), "0"])] * 2)
    (exp / "filelist.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return len(lines)


def run(args: argparse.Namespace) -> None:
    root = Path(args.rvc_root).resolve(strict=True)
    dataset = Path(args.dataset).resolve(strict=True)
    if not dataset.is_dir():
        raise NotADirectoryError(dataset)
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", args.name):
        raise ValueError("Model name must be 1-64 ASCII letters, digits, '-' or '_'")
    if not any(p.suffix.lower() in {".wav", ".mp3", ".flac", ".ogg", ".m4a"}
               for p in dataset.rglob("*" ) if p.is_file()):
        raise RuntimeError("Dataset must contain audio files")
    scripts = ["train/preprocess.py", "train/dataset/extract_f0.py",
               "train/dataset/extract_hubert_feature.py", "train/train.py"]
    if args.index == "on":
        scripts.append("train/train_index.py")
    for name in scripts:
        require_file(root / name, "Install the official RVC repository first.")
    conf_file = require_file(root / "configs" / "v2" / "48k.json")
    gen = require_file(root / "assets" / "pretrained_v2" / "f0G48k.pth",
                       "Download the official RVC v2 F0 pretrained checkpoints.")
    disc = require_file(root / "assets" / "pretrained_v2" / "f0D48k.pth",
                        "Download the official RVC v2 F0 pretrained checkpoints.")
    require_file(root / "assets" / "rmvpe" / "rmvpe.pt")
    # Model v2 uses the HuBERT content encoder; RVC's extractor checks its own dependencies.
    if not (root / "assets" / "hubert_base").is_dir():
        raise RuntimeError("Missing assets/hubert_base - download official RVC HuBERT files")
    exp = root / "logs" / args.name
    exp.mkdir(parents=True, exist_ok=True)
    if "|" in str(root) or "|" in str(dataset):
        raise ValueError("RVC paths must not contain '|'")
    print(f"[myVoice] experiment: {exp}", flush=True)
    print("[myVoice] stage 1/5: preprocess", flush=True)
    stage(root, "train/preprocess.py", dataset, 48000, args.workers, exp, "False", "3.7")
    print("[myVoice] stage 2/5: RMVPE F0 extraction", flush=True)
    if args.device == "cuda":
        stage(root, "train/dataset/extract_f0.py", "cuda", 1, 0, args.gpu,
              exp, "False")
    else:
        stage(root, "train/dataset/extract_f0.py", "cpu", exp, args.workers, "rmvpe")
    print("[myVoice] stage 3/5: HuBERT feature extraction", flush=True)
    if args.device == "cuda":
        stage(root, "train/dataset/extract_hubert_feature.py", "cuda", 1, 0,
              args.gpu, exp, "v2", "False")
    else:
        stage(root, "train/dataset/extract_hubert_feature.py", "cpu", 1, 0,
              exp, "v2", "False")
    count = build_filelist(root, exp)
    config_dest = exp / "config.json"
    if not config_dest.exists():
        config = json.loads(conf_file.read_text(encoding="utf-8"))
        config_dest.write_text(json.dumps(config, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"[myVoice] prepared {count} training clips", flush=True)
    print("[myVoice] stage 4/5: RVC v2 F0 training", flush=True)
    cmd = ["train/train.py", "-e", args.name, "-sr", "48k", "-f0", "1",
           "-bs", str(args.batch), "-te", str(args.epochs),
           "-se", str(args.save_every), "-pg", gen, "-pd", disc,
           "-l", "1", "-c", "0", "-sw", "1", "-v", "v2"]
    if args.device == "cuda":
        cmd += ["-g", str(args.gpu)]
    stage(root, *cmd)
    if args.index == "on":
        print("[myVoice] stage 5/5: FAISS index", flush=True)
        stage(root, "train/train_index.py", args.name, "v2", root / "assets" / "indices", args.workers)
    print(f"[myVoice] training complete: {exp}", flush=True)
    print("[myVoice] weights: " + str(root / "assets" / "weights"), flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="myVoice RVC v2 training bridge")
    parser.add_argument("--rvc-root", required=True)
    parser.add_argument("--dataset", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--epochs", type=int, default=200)
    parser.add_argument("--batch", type=int, default=4)
    parser.add_argument("--save-every", type=int, default=20)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--device", choices=["cpu", "cuda"], default="cuda")
    parser.add_argument("--gpu", type=int, default=0)
    parser.add_argument("--index", choices=["on", "off"], default="on")
    args = parser.parse_args()
    if not 1 <= args.epochs <= 10000 or not 1 <= args.batch <= 128:
        parser.error("epochs must be 1..10000 and batch 1..128")
    if not 1 <= args.workers <= 64 or not 1 <= args.save_every <= args.epochs:
        parser.error("workers must be 1..64, save-every must be 1..epochs")
    if not 0 <= args.gpu <= 31:
        parser.error("gpu must be 0..31")
    try:
        run(args)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"[myVoice] ERROR: {exc}", file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
