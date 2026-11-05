param(
    [ValidateSet("all","seed-vc","rvc")]
    [string]$Engine = "all",
    [string]$Version = "portable-python-v1"
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$platform = "windows-x64"
$engines = if ($Engine -eq "all") { @("seed-vc","rvc") } else { @($Engine) }
foreach ($one in $engines) {
    $filename = "portable-python-$platform-$one-cpu.zip"
    $url = "https://github.com/a289672082/myVoice/releases/download/$Version/$filename"
    $temp = Join-Path ([IO.Path]::GetTempPath()) ("myvoice-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $temp | Out-Null
    try {
        $archive = Join-Path $temp $filename
        $sha = "$archive.sha256"
        Write-Host "下载完整免安装运行环境：$one"
        Invoke-WebRequest -Uri $url -OutFile $archive
        Invoke-WebRequest -Uri "$url.sha256" -OutFile $sha
        $expected = ((Get-Content $sha -Raw) -split '\s+')[0].ToLowerInvariant()
        $actual = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($expected -ne $actual) { throw "SHA256 校验失败: $filename" }
        Expand-Archive -Path $archive -DestinationPath $repo -Force
        $executable = Join-Path $repo "runtime\$platform\$one\python.exe"
        if (!(Test-Path $executable)) { throw "运行环境不完整：$executable" }
        & $executable -c "import sys, torch; print(sys.version); print(torch.__version__)"
        if ($LASTEXITCODE -ne 0) { throw "便携 Python 运行失败: $one" }
        Write-Host "完成：$executable" -ForegroundColor Green
    }
    finally { Remove-Item $temp -Force -Recurse -ErrorAction SilentlyContinue }
}
