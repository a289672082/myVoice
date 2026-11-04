#!/usr/bin/env python3
"""验证仓库中检入的 VS2022 .sln / .vcxproj 无缺失路径、FTXUI 源码全部被引用。"""
from pathlib import Path
import xml.etree.ElementTree as ET
import sys

ROOT = Path(__file__).resolve().parents[1]
PROJECT_DIR = ROOT / "vs2022"
NS = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}
PROJECTS = ["myvoice_cli", "myvoice_core", "ftxui_component",
            "ftxui_dom", "ftxui_screen", "myvoice_shell_tests"]

def main():
    errors = []
    sln = (ROOT / "myVoice.sln").read_text(encoding="utf-8-sig")
    if "# Visual Studio Version 17" not in sln:
        errors.append("Solution must target Visual Studio 2022 (17)")
    for name in PROJECTS:
        project = PROJECT_DIR / (name + ".vcxproj")
        if not project.is_file():
            errors.append("Missing project: " + str(project))
            continue
        if name + ".vcxproj" not in sln:
            errors.append("Missing solution entry: " + name)
        xml = ET.parse(project).getroot()
        cl = {v.attrib["Include"].replace("\\", "/") for v in xml.findall(".//ms:ClCompile", NS)}
        hdr = {v.attrib["Include"].replace("\\", "/") for v in xml.findall(".//ms:ClInclude", NS)}
        for item in cl | hdr:
            if not (PROJECT_DIR / item).is_file():
                errors.append("Missing project source: " + name + " " + item)
        for item in xml.findall(".//ms:ProjectReference", NS):
            if not (PROJECT_DIR / item.attrib["Include"]).is_file():
                errors.append("Broken project ref: " + name + " " + item.attrib["Include"])
        if name.startswith("ftxui_"):
            group = name.split("_", 1)[1]
            base = ROOT / "third_party/ftxui/src/ftxui"
            expected = (list((base / "screen").glob("*.cpp")) + list((base / "util").glob("*.cpp"))) if group == "screen" else list((base / group).glob("*.cpp"))
            actual = {(PROJECT_DIR / f).resolve() for f in cl}
            for f in expected:
                if f.resolve() not in actual:
                    errors.append("FTXUI source not in VS project: " + str(f))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("VS2022 solution verified: " + ", ".join(PROJECTS))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
