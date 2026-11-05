#!/usr/bin/env python3
"""Build a full, relocatable CPython interpreter and dependencies for RVC/Seed-VC."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ENGINES = {
 "seed-vc": ("3.10", "2.4.0", "0.19.0", "third_party/seed-vc/requirements.txt", "hydra"),
 "rvc": ("3.12", "2.4.1", "0.19.1", "third_party/rvc/requirments_cpu_py312.txt", "faiss"),
}

def run(command, env=None):
    print("+", " ".join(str(x) for x in command), flush=True)
    subprocess.run(list(map(str, command)), check=True, env=env)

def executable(folder):
    return folder / ("python.exe" if os.name == "nt" else "bin/python3")

def sanitized_requirements(source, dest):
    # Seed-VC's original requirements specify both CUDA nightly and pinned stable Torch.
    # Use CPU PyTorch once, and install the remaining original Python dependencies.
    output = []
    for line in source.read_text(encoding="utf-8").splitlines():
        lower = line.strip().lower()
        if not lower or lower.startswith(("#", "--index-url", "--extra-index-url")):
            continue
        if lower.startswith(("torch ", "torch=", "torchvision", "torchaudio")):
            continue
        output.append(line)
    dest.write_text("\n".join(output) + "\n", encoding="utf-8")
    print("Remaining original requirements:", len(output), flush=True)

def verify(py, extra):
    run([py, "-c", "import sys,torch,torchaudio,torchvision,numpy,librosa,transformers;"
                      "import " + extra + ";"
                      "print(sys.version,torch.__version__,sys.executable)"])

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", choices=ENGINES, required=True)
    parser.add_argument("--dist", default=str(ROOT / "dist"))
    args = parser.parse_args()
    if sys.maxsize <= 2 ** 32:
        raise RuntimeError("Only x86-64 is supported")
    if sys.platform == "win32":
        platform = "windows-x64"
    elif sys.platform == "linux":
        platform = "linux-x64"
    else:
        raise RuntimeError("Only Windows and Linux are supported")
    version, torch, vision, requirement, check = ENGINES[args.engine]
    basename = f"portable-python-{platform}-{args.engine}-cpu"
    dist = Path(args.dist).resolve()
    dist.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="myvoice-runtime-") as td:
        tmp = Path(td)
        managed = tmp / "managed"
        env = dict(os.environ, UV_PYTHON_INSTALL_DIR=str(managed),
                   UV_LINK_MODE="copy", UV_CACHE_DIR=str(tmp / "cache"), UV_NO_PROGRESS="1")
        run(["uv", "python", "install", version], env)
        # uv creates a minor-version alias (3.10) alongside the real version (3.10.22).
        # Bundle the actual versioned directory, never duplicate the alias.
        candidates = [x for x in managed.iterdir() if x.is_dir() and
                      x.name.startswith("cpython-" + version + ".") and
                      executable(x).is_file()]
        if len(candidates) != 1:
            raise RuntimeError(f"Expected one actual standalone CPython: {candidates}")
        relative = Path("runtime") / platform / args.engine
        staging = tmp / "staging"
        location = staging / relative
        shutil.copytree(candidates[0], location, symlinks=True)
        py = executable(location)
        run([py, "-c", f"import sys; assert sys.version_info[:2]=={tuple(map(int,version.split('.')))}"])
        run(["uv", "pip", "install", "--python", py, "--torch-backend", "cpu",
             f"torch=={torch}", f"torchvision=={vision}", f"torchaudio=={torch}"], env)
        req = tmp / "requirements-portable.txt"
        sanitized_requirements(ROOT / requirement, req)
        run(["uv", "pip", "install", "--python", py, "--torch-backend", "cpu", "-r", req], env)
        verify(py, check)
        (location / "myvoice-python-runtime.json").write_text(
            json.dumps({"engine": args.engine, "platform": platform, "python": version,
                        "acceleration": "cpu", "source_requirements": requirement}, indent=2),
            encoding="utf-8")
        archive = dist / (basename + ".zip")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED,
                             compresslevel=6, allowZip64=True) as z:
            for f in sorted(staging.rglob("*")):
                if f.is_file():
                    z.write(f, f.relative_to(staging).as_posix())
        h = hashlib.sha256()
        with archive.open("rb") as f:
            for block in iter(lambda: f.read(8 * 1024 * 1024), b""):
                h.update(block)
        (dist / (basename + ".zip.sha256")).write_text(
            f"{h.hexdigest()}  {archive.name}\n", encoding="utf-8")
        # Verify the final ZIP after moving to a different directory.
        relocated = tmp / "relocated"
        with zipfile.ZipFile(archive) as z:
            failed = z.testzip()
            if failed:
                raise RuntimeError("Corrupt ZIP member: " + failed)
            z.extractall(relocated)
        target = relocated / relative / py.relative_to(location)
        if os.name != "nt":
            target.chmod(target.stat().st_mode | 0o111)
        verify(target, check)
        print("PASS, relocated self-contained runtime:", archive,
              archive.stat().st_size, "bytes", flush=True)

if __name__ == "__main__":
    main()
