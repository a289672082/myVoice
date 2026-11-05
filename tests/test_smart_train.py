#!/usr/bin/env python3
"""不访问互联网、不加载真实大模型的智能训练测试。"""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import numpy as np
import soundfile as sf

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "smart_train.py"
SPEC = importlib.util.spec_from_file_location("smart_train", SCRIPT)
st = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(st)


class FakeCuda:
    @staticmethod
    def is_available(): return False
    @staticmethod
    def device_count(): return 0


class FakeTorch:
    cuda = FakeCuda()


class Tests(unittest.TestCase):
    def test_hardware_auto_cpu_and_explicit_cuda_error(self):
        plan = st.select_hardware("rvc", "auto", 0, -1, torch_module=FakeTorch)
        self.assertEqual(plan["device"], "cpu")
        self.assertEqual(plan["batch"], 1)
        with self.assertRaisesRegex(RuntimeError, "CUDA"):
            st.select_hardware("rvc", "cuda", 0, -1, torch_module=FakeTorch)

    def test_quality_filter_and_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dataset = root / "raw"
            dataset.mkdir()
            rate = 16000
            signal = np.sin(np.linspace(0, 8 * 2 * np.pi * 110, rate * 8)).astype(np.float32) * .23
            sf.write(dataset / "ok.wav", signal, rate)
            sf.write(dataset / "silent.wav", np.zeros(rate * 4, dtype=np.float32), rate)
            sf.write(dataset / "clipped.wav", np.ones(rate * 4, dtype=np.float32), rate)
            report = st.prepare_dataset(dataset, root / "processed" / "clean", "seed-vc")
            self.assertEqual(len(report["accepted"]), 1)
            self.assertEqual(len(report["rejected"]), 2)
            self.assertEqual(len(list((root / "processed" / "clean").glob("*.wav"))), 1)
            self.assertTrue((root / "processed" / "myvoice_quality_report.json").is_file())
            with self.assertRaises(ValueError):
                st.prepare_dataset(dataset, dataset / "nested", "seed-vc")

    def test_download_assets_from_trusted_resolver_and_offline(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cached = root / "cached.pth"
            cached.write_bytes(b"x" * 100)
            destination = root / "models" / "test.pth"
            result = st.required_asset("trusted/repo", "model.pth", destination, 90,
                                       downloader=lambda **k: cached)
            self.assertEqual(result, destination.resolve())
            self.assertEqual(destination.read_bytes(), cached.read_bytes())
            result = st.required_asset("trusted/repo", "model.pth", destination, 90,
                                       offline=True)
            self.assertEqual(result, destination.resolve())
            with self.assertRaises(FileNotFoundError):
                st.required_asset("trusted/repo", "missing.pth", root / "missing.pth",
                                  90, offline=True)

    def test_register_model_and_reject_nonexistent(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            weight = root / "voice.pth"
            ar = root / "ar.pth"
            weight.write_bytes(b"x" * 40)
            ar.write_bytes(b"a" * 40)
            index = root / "models" / "index.tsv"
            st.register_model(index, "seed-vc", "male_v1", weight, ar)
            st.register_model(index, "seed-vc", "male_v1", weight, ar)
            self.assertEqual(len(index.read_text(encoding="utf-8").splitlines()), 1)
            self.assertIn("male_v1", index.read_text(encoding="utf-8"))
            with self.assertRaises(ValueError):
                st.register_model(index, "rvc", "../invalid", weight)
            with self.assertRaises(FileNotFoundError):
                st.register_model(index, "rvc", "not_found", root / "missing.pth")

    def test_rvc_and_seed_v2_end_to_end_mock_registration(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = root / "voice"
            data.mkdir()
            sample_rate = 16000
            t = np.arange(sample_rate * 8) / sample_rate
            sf.write(data / "sample.wav", 0.3 * np.sin(2 * np.pi * 115 * t), sample_rate)
            registry = root / "models" / "index.tsv"
            for engine in ("rvc", "seed-vc"):
                backend = root / engine
                marker = backend / ("train/train.py" if engine == "rvc" else "train_v2.py")
                marker.parent.mkdir(parents=True, exist_ok=True)
                marker.write_text("# mocked training backend\n")
                cfg = st.parse_args(["--engine", engine, "--root", str(backend),
                    "--dataset", str(data), "--name", engine.replace("-", "_"),
                    "--registry", str(registry), "--device", "cpu", "--epochs", "2",
                    "--save-every", "1", "--steps", "2"])
                def pretend_stage(cmd, cwd, env=None):
                    if engine == "rvc":
                        target = backend / "assets/weights/seed_rvc.pth"
                        # The run name is rvc, not seed_rvc.
                        target = backend / "assets/weights/rvc.pth"
                    else:
                        target = backend / "runs/seed_vc/CFM_epoch_0_step_2.pth"
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(b"w" * 64)
                fake_ar = backend / "pretrained_ar.pth"
                fake_ar.write_bytes(b"a" * 64)
                with mock.patch.object(st, "ensure_pretrained",
                     return_value={"cfm_small.pth": str(fake_ar), "ar_base.pth": str(fake_ar)}), \
                     mock.patch.object(st, "stage", side_effect=pretend_stage), \
                     mock.patch.object(st, "select_hardware", return_value={
                         "device": "cpu", "gpu": 0, "batch": 1,
                         "workers": 0, "vram_gb": 0}):
                    st.execute(cfg)
            models = registry.read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(models), 2)
            self.assertTrue(any(s.startswith("rvc\trvc\t") for s in models))
            self.assertTrue(any(s.startswith("seed-vc\tseed_vc\t") for s in models))
            for engine, name in (("rvc", "rvc"), ("seed-vc", "seed_vc")):
                root_dir = root / engine / ("logs" if engine == "rvc" else "runs") / name
                progress = (root_dir / "myvoice_progress.txt").read_text(encoding="utf-8")
                self.assertIn("成功", progress)
                self.assertIn("5/5", progress)

if __name__ == "__main__":
    unittest.main()
