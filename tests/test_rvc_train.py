#!/usr/bin/env python3
"""Test myVoice RVC bridge without downloading PyTorch or RVC weights."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> None:
    bridge = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="myvoice rvc test ") as tmp:
        base = Path(tmp)
        rvc = base / "RVC project with spaces"
        dataset = base / "voice dataset"
        dataset.mkdir()
        (dataset / "voice.wav").write_bytes(b"RIFF fake training input")
        for x in ["assets/pretrained_v2/f0G48k.pth", "assets/pretrained_v2/f0D48k.pth",
                  "assets/rmvpe/rmvpe.pt", "assets/hubert_base/dummy.json"]:
            p = rvc / x
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"placeholder")
        (rvc / "configs/v2").mkdir(parents=True)
        (rvc / "configs/v2/48k.json").write_text(json.dumps({"train": {"batch_size": 4}}))
        def script(name: str, code: str) -> None:
            p = rvc / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(code)
        script("train/preprocess.py", '''import sys
from pathlib import Path
exp=Path(sys.argv[4]);
for d in ('0_gt_wavs','1_16k_wavs'):(exp/d).mkdir(parents=True,exist_ok=True)
(exp/'0_gt_wavs'/'clip.wav').write_bytes(b'RIFF some audio')
(exp/'1_16k_wavs'/'clip.wav').write_bytes(b'RIFF some audio')
''')
        script("train/dataset/extract_f0.py", '''import sys
from pathlib import Path
exp=Path(sys.argv[2] if sys.argv[1]=='cpu' else sys.argv[5]);
for d in ('2a_f0','2b-f0nsf'):(exp/d).mkdir(parents=True,exist_ok=True)
for d in ('2a_f0','2b-f0nsf'):(exp/d/'clip.wav.npy').write_bytes(b'fake numpy')
''')
        script("train/dataset/extract_hubert_feature.py", '''import sys
from pathlib import Path
exp=Path(sys.argv[4] if sys.argv[1]=='cpu' else sys.argv[5]);
(exp/'3_feature768').mkdir(parents=True,exist_ok=True)
(exp/'3_feature768'/'clip.npy').write_bytes(b'fake numpy')
''')
        script("train/train.py", '''import sys
from pathlib import Path
args=sys.argv
name=args[args.index('-e')+1]
exp=Path('logs')/name
assert 'clip.wav' in (exp/'filelist.txt').read_text(), 'bad filelist'
assert (exp/'config.json').exists(), 'missing config'
(Path('assets')/'weights').mkdir(parents=True,exist_ok=True)
(Path('assets')/'weights'/f'{name}.pth').write_bytes(b'model')
''')
        script("train/train_index.py", '''import sys
from pathlib import Path
assert Path('logs',sys.argv[1],'filelist.txt').is_file()
''')
        cmd = [sys.executable, str(bridge), "--rvc-root", str(rvc),
               "--dataset", str(dataset), "--name", "test_voice",
               "--epochs", "3", "--batch", "2", "--save-every", "1",
               "--workers", "2", "--device", "cpu", "--index", "on"]
        ok = subprocess.run(cmd, capture_output=True, text=True)
        assert ok.returncode == 0, (ok.stdout, ok.stderr)
        exp = rvc / 'logs' / 'test_voice'
        assert (exp / 'filelist.txt').is_file()
        assert (rvc / 'assets' / 'weights' / 'test_voice.pth').is_file()
        bad = subprocess.run(cmd[:cmd.index('--name')+1] + ['../escape'] + cmd[cmd.index('--epochs'):],
                             capture_output=True, text=True)
        assert bad.returncode != 0 and 'name' in (bad.stdout+bad.stderr).lower()
        missing = subprocess.run([sys.executable, str(bridge), "--rvc-root", str(rvc),
              "--dataset", str(base/'not-found'), "--name", "test_voice"], capture_output=True, text=True)
        assert missing.returncode != 0
    print('RVC training mock pipeline passed')


if __name__ == '__main__':
    main()
