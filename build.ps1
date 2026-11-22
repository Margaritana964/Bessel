# Bessel · 构建脚本
#
# 用法：
#   pwsh -File build.ps1               增量构建
#   pwsh -File build.ps1 -Clean        清理后全量构建
#   pwsh -File build.ps1 -ConfigureOnly 只跑 CMake 配置
#
# 注意：本机沙箱会卡住 ninja 的子进程等待，构建这一步必须放开沙箱运行。

param(
    [switch]$Clean,
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$build = Join-Path $root 'build'

# --- 找 VS Build Tools -------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "找不到 vswhere.exe —— 先装 Visual Studio Build Tools，勾选「使用 C++ 的桌面开发」"
}

$vsPath = & $vswhere -latest -products * -property installationPath
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    throw "找不到 vcvars64.bat —— C++ 工具链没装全：$vcvars"
}

# --- 把 vcvars64 的环境灌进当前进程 -----------------------------------------
Write-Host ">>> 载入 MSVC 环境" -ForegroundColor Cyan
foreach ($line in (cmd /c "`"$vcvars`" >nul 2>&1 && set")) {
    if ($line -match '^([^=][^=]*)=(.*)$') {
        Set-Item -Path ("env:" + $matches[1]) -Value $matches[2]
    }
}

$cmakeBin = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaBin = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$env:PATH = "$cmakeBin;$ninjaBin;$env:PATH"

Write-Host "    cmake : $((Get-Command cmake).Source)"
Write-Host "    ninja : $((Get-Command ninja).Source)"
Write-Host "    cl    : $((Get-Command cl).Source)"

# --- 配置 --------------------------------------------------------------------
if ($Clean -and (Test-Path $build)) {
    Write-Host ">>> 清理 build/" -ForegroundColor Cyan
    Remove-Item $build -Recurse -Force
}

if ($Clean -or -not (Test-Path (Join-Path $build 'build.ninja'))) {
    Write-Host ">>> CMake 配置" -ForegroundColor Cyan
    cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败（退出码 $LASTEXITCODE）" }
}

if ($ConfigureOnly) {
    Write-Host ">>> 只配置，结束" -ForegroundColor Green
    exit 0
}

# --- 构建 --------------------------------------------------------------------
Write-Host ">>> 编译" -ForegroundColor Cyan
$sw = [Diagnostics.Stopwatch]::StartNew()
cmake --build $build --parallel
if ($LASTEXITCODE -ne 0) { throw "编译失败（退出码 $LASTEXITCODE）" }
Write-Host ("    用时 {0:N1}s" -f $sw.Elapsed.TotalSeconds)

# --- 收集产物 ----------------------------------------------------------------
$artefacts = Join-Path $build 'Bessel_artefacts\Release'
$dist      = Join-Path $root 'dist'

if (Test-Path $artefacts) {
    New-Item -ItemType Directory -Force (Join-Path $dist 'VST3') | Out-Null

    $vst3Helper = Join-Path $build 'vst3_helpers\Bessel\vst3_helper.exe'
    if (Test-Path $vst3Helper) {
        $resDir = Join-Path $artefacts 'VST3\Bessel.vst3\Contents\Resources'
        if (Test-Path (Join-Path $artefacts 'VST3\Bessel.vst3')) {
            New-Item -ItemType Directory -Force $resDir | Out-Null
            $jsonPath = Join-Path $resDir 'moduleinfo.json'
            & $vst3Helper | Set-Content -Path $jsonPath -Encoding utf8
        }
    }

    Get-ChildItem $artefacts -Recurse -Filter '*.vst3' -Directory -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host ">>> 复制 $($_.Name) → dist\VST3" -ForegroundColor Cyan
        Copy-Item $_.FullName (Join-Path $dist 'VST3') -Recurse -Force
    }

    Get-ChildItem $artefacts -Recurse -Filter '*.exe' -File -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item $_.FullName $dist -Force
    }
}

# --- 冒烟测试 + 宿主扫描测试 -------------------------------------------------
foreach ($test in @(
    @{ Name = 'PmEffectTest';   Exe = Join-Path $build 'PmEffectTest_artefacts\Release\PmEffectTest.exe' },
    @{ Name = 'PluginScanTest'; Exe = Join-Path $build 'PluginScanTest_artefacts\Release\PluginScanTest.exe' }
)) {
    if (Test-Path $test.Exe) {
        Write-Host ">>> $($test.Name)" -ForegroundColor Cyan
        & $test.Exe
        if ($LASTEXITCODE -ne 0) { throw "$($test.Name) 未通过（退出码 $LASTEXITCODE）" }
    }
    else {
        Write-Warning "找不到 $($test.Exe)，跳过"
    }
}

Write-Host "`n>>> 完成。产物在 dist\ 和 build\Bessel_artefacts\Release\" -ForegroundColor Green

# --- 界面预览图 --------------------------------------------------------------
$snapshotExe = Join-Path $build 'UiSnapshot_artefacts\Release\UiSnapshot.exe'
if (Test-Path $snapshotExe) {
    & $snapshotExe | Out-Null
    $png = Join-Path $build 'UiSnapshot_artefacts\Release\ui-snapshot.png'
    if (Test-Path $png) {
        Copy-Item $png (Join-Path $root 'ui-preview.png') -Force
        Write-Host ">>> 界面预览已更新 ui-preview.png" -ForegroundColor Green
    }
}
