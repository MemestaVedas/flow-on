# build.ps1 — FLOW-ON! full build script
# Run from the project root in a Developer PowerShell for VS 2022.
# Requires: cmake, Visual Studio 2022, and the Whisper model downloaded.
#
# Usage:
#   pwsh build.ps1           — Debug build
#   pwsh build.ps1 -Release  — Release build
#   pwsh build.ps1 -Installer — Release + NSIS installer (requires makensis in PATH)

param(
    [switch]$Release,
    [switch]$Installer,
    [switch]$CUDA        # Enable NVIDIA GPU acceleration
)

$ErrorActionPreference = "Stop"
$cfg = if ($Release -or $Installer) { "Release" } else { "Debug" }

Write-Host "=== FLOW-ON! Build ($cfg) ===" -ForegroundColor Cyan

# 1. Download model if missing
$modelPath = "models\ggml-tiny.en.bin"
if (-not (Test-Path $modelPath)) {
    Write-Host "[1/4] Downloading Whisper tiny.en model (~75 MB)..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Force models | Out-Null
    Invoke-WebRequest `
        -Uri "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin" `
        -OutFile $modelPath `
        -UseBasicParsing
} else {
    Write-Host "[1/4] Model already present." -ForegroundColor Green
}

# 2. Generate placeholder icons if missing
if (-not (Test-Path "assets\app_icon.ico")) {
    Write-Host "[2/4] Generating placeholder icons..." -ForegroundColor Yellow
    pwsh assets\generate_icons.ps1
} else {
    Write-Host "[2/4] Icons already present." -ForegroundColor Green
}

# 3. CMake configure + build
Write-Host "[3/4] Configuring CMake..." -ForegroundColor Yellow
$cmakeArgs = @(
    "-B", "build",
    "-G", "Visual Studio 17 2022",
    "-A", "x64"
)
if ($CUDA) {
    $cmakeArgs += @("-DWHISPER_CUBLAS=ON", "-DGGML_CUDA=ON")
    Write-Host "      CUDA acceleration ENABLED" -ForegroundColor Magenta
}
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host "[3/4] Building ($cfg)..." -ForegroundColor Yellow
& cmake --build build --config $cfg --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "[3/4] Build succeeded." -ForegroundColor Green

# 4. (Optional) NSIS installer
if ($Installer) {
    Write-Host "[4/4] Building installer..." -ForegroundColor Yellow

    # Check for Windows App SDK runtime redistributable
    if (-not (Test-Path "redist\WindowsAppRuntimeInstall-x64.exe")) {
        Write-Host "      Downloading Windows App SDK runtime redistributable..." -ForegroundColor Yellow
        New-Item -ItemType Directory -Force redist | Out-Null
        Invoke-WebRequest `
            -Uri "https://aka.ms/windowsappsdk/1.5/latest/windowsappruntimeinstall-x64.exe" `
            -OutFile "redist\WindowsAppRuntimeInstall-x64.exe" `
            -UseBasicParsing
    }

    if (-not (Test-Path "LICENSE.txt")) {
        Set-Content "LICENSE.txt" "MIT License`n`nCopyright 2025 Your Name`n`nPermission is hereby granted..."
    }

    & makensis "installer\flow-on.nsi"
    if ($LASTEXITCODE -ne 0) { throw "NSIS build failed" }
    Write-Host "    Installer: FLOW-ON-Setup.exe" -ForegroundColor Green
} else {
    Write-Host "[4/4] Skipping installer (use -Installer flag to build it)." -ForegroundColor Gray
}

Write-Host ""
Write-Host "Done!  Binary: build\$cfg\flow-on.exe" -ForegroundColor Cyan
