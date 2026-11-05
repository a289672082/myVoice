#!/usr/bin/env python3
"""GPU 安装器的无 GPU、无网络测试：模拟驱动和计划验证，不下载真实 torch。"""
import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

script = Path(__file__).resolve().parents[1] / "scripts/setup_gpu_runtime.py"
spec = importlib.util.spec_from_file_location("setup_gpu_runtime", script)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class Result:
    def __init__(self, out):
        self.stdout = out

class Tests(unittest.TestCase):
    def test_detect_gpu_and_plan(self):
        with patch.object(mod.sys, "platform", "linux"):
            fake = lambda *a, **kw: Result("NVIDIA RTX 4090, 8.9, 550.54\n")
            gpu = mod.detect_nvidia(fake)
            self.assertEqual(gpu["backend"], "cu121")
            self.assertEqual(mod.plan("rvc", gpu)["torch"], "2.4.1")
            self.assertEqual(mod.plan("seed-vc", gpu)["torch"], "2.4.0")
    def test_driver_and_arch_safety(self):
        with patch.object(mod.sys, "platform", "linux"):
            with self.assertRaisesRegex(RuntimeError, "驱动"):
                mod.detect_nvidia(lambda *a, **kw: Result("RTX 4090, 8.9, 500.01"))
            with self.assertRaisesRegex(RuntimeError, "验证范围"):
                mod.detect_nvidia(lambda *a, **kw: Result("RTX 5090, 12.0, 570.01"))
    def test_no_gpu_error(self):
        with self.assertRaisesRegex(RuntimeError, "NVIDIA"):
            mod.detect_nvidia(lambda *a, **kw: (_ for _ in ()).throw(FileNotFoundError()))
    def test_dry_run_never_downloads(self):
        with patch.object(mod, "detect_nvidia", return_value={
            "name": "fake", "compute_capability": "8.9", "driver": "550.54",
            "backend": "cu121"}), patch.object(mod, "fetch_verified_uv") as download:
            self.assertTrue(str(mod.prepare("rvc", dry_run=True)).endswith("rvc-gpu"))
            download.assert_not_called()

if __name__ == "__main__":
    unittest.main()
