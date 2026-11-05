#!/usr/bin/env bash
set -euo pipefail
ENGINE="all"
if test "$#" -ge 1; then ENGINE="$1"; fi
TAG="portable-python-v1"
if test "$#" -ge 2; then TAG="$2"; fi
case "$ENGINE" in
  all) ENGINES="seed-vc rvc" ;;
  seed-vc|rvc) ENGINES="$ENGINE" ;;
  *) echo "Usage: bash scripts/get_portable_python.sh [all|seed-vc|rvc] [release-tag]" >&2; exit 2 ;;
esac
REPO="$(cd "$(dirname "$0")/.." && pwd)"
command -v unzip >/dev/null || { echo "Missing unzip" >&2; exit 2; }
download() {
  if command -v curl >/dev/null; then curl --fail --location --retry 3 "$1" -o "$2"
  elif command -v wget >/dev/null; then wget -q "$1" -O "$2"
  else echo "Need curl or wget" >&2; exit 2
  fi
}
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for engine in $ENGINES; do
  file="portable-python-linux-x64-$engine-cpu.zip"
  url="https://github.com/a289672082/myVoice/releases/download/$TAG/$file"
  echo "下载 $engine 自包含 Python + 第三方包 ..."
  download "$url" "$tmp/$file"
  download "$url.sha256" "$tmp/$file.sha256"
  (cd "$tmp" && sha256sum -c "$file.sha256")
  unzip -oq "$tmp/$file" -d "$REPO"
  python="$REPO/runtime/linux-x64/$engine/bin/python3"
  test -f "$python" || { echo "Missing runtime: $python" >&2; exit 1; }
  chmod +x "$python"
  "$python" -c 'import sys,torch; print(sys.version); print(torch.__version__)'
  echo "完成: $python"
done
