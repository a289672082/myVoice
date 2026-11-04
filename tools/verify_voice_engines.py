#!/usr/bin/env python3
"""Verify checked-in RVC and Seed-VC source snapshots without network access."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = (
    "third_party/rvc/LICENSE",
    "third_party/rvc/README.md",
    "third_party/rvc/infer/cli.py",
    "third_party/rvc/train/preprocess.py",
    "third_party/rvc/train/train.py",
    "third_party/rvc/train/train_index.py",
    "third_party/rvc/train/dataset/extract_f0.py",
    "third_party/rvc/train/dataset/extract_hubert_feature.py",
    "third_party/rvc/configs/v2/48k.json",
    "third_party/seed-vc/LICENSE",
    "third_party/seed-vc/README.md",
    "third_party/seed-vc/inference.py",
    "third_party/seed-vc/inference_v2.py",
    "third_party/seed-vc/train.py",
    "third_party/seed-vc/train_v2.py",
    "third_party/seed-vc/configs/v2/vc_wrapper.yaml",
    "third_party/seed-vc/modules/v2/vc_wrapper.py",
    "third_party/ENGINES.md",
)
def main():
    missing = [f for f in REQUIRED if not (ROOT / f).is_file() or (ROOT / f).stat().st_size == 0]
    metadata = [f for f in ("third_party/rvc/.git", "third_party/seed-vc/.git")
                if (ROOT / f).exists()]
    if missing or metadata:
        for path in missing: print("Missing vendored source: " + path, file=sys.stderr)
        for path in metadata: print("Nested git checkout/submodule forbidden: " + path, file=sys.stderr)
        return 1
    print("Embedded RVC and Seed-VC verified (" + str(len(REQUIRED)) + " required files).")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
