# install.ps1 - FLOW-ON! One-Click Installer for End Users
# This script downloads, builds, and installs FLOW-ON! automatically
# Run from PowerShell as Administrator (for system-wide install) or regular user (for user install)
#
# Usage:
#   irm https://raw.githubusercontent.com/YOUR_REPO/main/install.ps1 | iex
#   OR save and run: .\install.ps1

param(
    [switch]$SystemWide,    # Install for all users (requires admin)
    [switch]$NoBuild,       # Skip build, just install pre-built binary
    [switch]$CUDA,          # Enable NVIDIA GPU acceleration
    [string]$InstallDir = ""  # Custom installation directory
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "Continue"

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------
$AppName = "FLOW-ON!"
$AppVersion = "1.0.0"
$RepoUrl = "https://github.com/MemestaVedas/flow-on"
$ReleaseUrl = "$RepoUrl/releases/download/v$AppVersion"

# Default install paths
$DefaultUserInstallDir = "$env:LOCALAPPDATA\Programs\FLOW-ON"
$DefaultSystemInstallDir = "$env:ProgramFiles\FLOW-ON"

# Determine install directory
if ($InstallDir) {
    $TargetDir = $InstallDir
} elseif ($SystemWide) {
    $TargetDir = $DefaultSystemInstallDir
} else {
    $TargetDir = $DefaultUserInstallDir
}

# ------------------------------------------------------------------------------
# Helper Functions
# ------------------------------------------------------------------------------
function Test-Admin {
    $currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Write-Header($text) {
    Write-Host "`n=== $text ===" -ForegroundColor Cyan
}

function Write-Step($text) {
    Write-Host "[+] $text" -ForegroundColor Yellow
}

function Write-Success($text) {
    Write-Host "[OK] $text" -ForegroundColor Green
}

function Write-Error($text) {
    Write-Host "[ERR] $text" -ForegroundColor Red
}

function Test-Command($cmd) {
    return [bool](Get-Command -Name $cmd -ErrorAction SilentlyContinue)
}

function Download-File($url, $outFile) {
    $maxRetries = 3
    $retryCount = 0
    
    while ($retryCount -lt $maxRetries) {
        try {
            Invoke-WebRequest -Uri $url -OutFile $outFile -UseBasicParsing -ErrorAction Stop
            return $true
        } catch {
            $retryCount++
            if ($retryCount -eq $maxRetries) {
                throw "Failed to download after $maxRetries attempts: $url"
            }
            Write-Host "  Retry $retryCount/$maxRetries..." -ForegroundColor DarkYellow
            Start-Sleep -Seconds 2
        }
    }
    return $false
}

# ------------------------------------------------------------------------------
# Pre-flight Checks
# ------------------------------------------------------------------------------
Write-Header "$AppName Installer v$AppVersion"

# Check Windows version
$osInfo = Get-CimInstance Win32_OperatingSystem
if ($osInfo.Version -lt "10.0.19041") {
    Write-Error "Windows 10 version 2004 or later is required"
    exit 1
}
Write-Success "Windows version check passed ($($osInfo.Caption))"

# Check for admin if system-wide install requested
if ($SystemWide -and -not (Test-Admin)) {
    Write-Error "Administrator privileges required for system-wide installation"
    Write-Host "Please run PowerShell as Administrator and try again." -ForegroundColor Yellow
    exit 1
}

# Check for required tools if building from source
if (-not $NoBuild) {
    Write-Step "Checking prerequisites..."
    
    # Check for Visual Studio 2022
    $vsPath = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    $vsEnterprise = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    $vsProfessional = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    $vsBuildTools = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    
    $hasVS = (Test-Path $vsPath) -or (Test-Path $vsEnterprise) -or (Test-Path $vsProfessional) -or (Test-Path $vsBuildTools)
    
    if (-not $hasVS) {
        Write-Error "Visual Studio 2022 or Build Tools not found"
        Write-Host "`nPlease install one of the following:" -ForegroundColor Yellow
        Write-Host "  1. Visual Studio 2022 Community (free): https://visualstudio.microsoft.com/downloads/"
        Write-Host "  2. Visual Studio 2022 Build Tools: https://aka.ms/vs/17/release/vs_BuildTools.exe"
        Write-Host "`nRequired workloads: 'Desktop development with C++'" -ForegroundColor Yellow
        exit 1
    }
    Write-Success "Visual Studio 2022 found"
    
    # Check for CMake
    if (-not (Test-Command "cmake")) {
        Write-Error "CMake not found. Please install CMake 3.20 or later."
        Write-Host "Download from: https://cmake.org/download/" -ForegroundColor Yellow
        exit 1
    }
    
    $cmakeVersion = (cmake --version) | Select-Object -First 1
    Write-Success "CMake found ($cmakeVersion)"
    
    # Check for Git
    if (-not (Test-Command "git")) {
        Write-Error "Git not found. Please install Git."
        Write-Host "Download from: https://git-scm.com/download/win" -ForegroundColor Yellow
        exit 1
    }
    Write-Success "Git found"
}

# ------------------------------------------------------------------------------
# Installation
# ------------------------------------------------------------------------------
Write-Header "Installing to: $TargetDir"

# Create installation directory
if (-not (Test-Path $TargetDir)) {
    New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
}

if ($NoBuild) {
    # Download pre-built release
    Write-Step "Downloading pre-built release..."
    $zipUrl = "$ReleaseUrl/FLOW-ON-Windows-x64.zip"
    $zipPath = "$env:TEMP\FLOW-ON-Windows-x64.zip"
    
    try {
        Download-File $zipUrl $zipPath
        Expand-Archive -Path $zipPath -DestinationPath $TargetDir -Force
        Remove-Item $zipPath -ErrorAction SilentlyContinue
        Write-Success "Downloaded and extracted to $TargetDir"
    } catch {
        Write-Error "Failed to download release. Build from source instead?"
        Write-Host "Run: .\install.ps1 (without -NoBuild)" -ForegroundColor Yellow
        exit 1
    }
} else {
    # Build from source
    $buildDir = "$env:TEMP\flow-on-build-$(Get-Random)"
    
    Write-Step "Cloning repository..."
    git clone --recursive $RepoUrl $buildDir
    if ($LASTEXITCODE -ne 0) {
        # Try without recursive first, then init submodules
        git clone $RepoUrl $buildDir
        Set-Location $buildDir
        git submodule update --init --recursive
    }
    Write-Success "Repository cloned to $buildDir"
    
    Write-Step "Building $AppName (this may take a few minutes)..."
    Set-Location $buildDir
    
    # Run build script
    $buildArgs = @("-Release")
    if ($CUDA) { $buildArgs += "-CUDA" }
    
    try {
        & .\build.ps1 @buildArgs
        if ($LASTEXITCODE -ne 0) { throw "Build failed" }
        Write-Success "Build completed successfully"
    } catch {
        Write-Error "Build failed: $_"
        exit 1
    }
    
    # Copy built files to install directory
    Write-Step "Installing files..."
    $sourceDir = "$buildDir\build\Release"
    
    # Copy executable
    Copy-Item "$sourceDir\flow-on.exe" $TargetDir -Force
    
    # Copy models
    if (-not (Test-Path "$TargetDir\models")) {
        New-Item -ItemType Directory -Force -Path "$TargetDir\models" | Out-Null
    }
    Copy-Item "$buildDir\models\ggml-tiny.en.bin" "$TargetDir\models\" -Force -ErrorAction SilentlyContinue
    
    # Copy assets
    if (Test-Path "$buildDir\assets") {
        if (-not (Test-Path "$TargetDir\assets")) {
            New-Item -ItemType Directory -Force -Path "$TargetDir\assets" | Out-Null
        }
        Copy-Item "$buildDir\assets\*.ico" "$TargetDir\assets\" -Force -ErrorAction SilentlyContinue
    }
    
    # Copy DLLs
    Get-ChildItem "$sourceDir\*.dll" | ForEach-Object {
        Copy-Item $_.FullName $TargetDir -Force
    }
    
    # Cleanup build directory
    Set-Location $env:TEMP
    Remove-Item $buildDir -Recurse -Force -ErrorAction SilentlyContinue
    Write-Success "Files installed to $TargetDir"
}

# ------------------------------------------------------------------------------
# Create Shortcuts
# ------------------------------------------------------------------------------
Write-Step "Creating shortcuts..."

# Start Menu shortcut
$startMenuDir = if ($SystemWide) {
    "$env:ALLUSERSPROFILE\Microsoft\Windows\Start Menu\Programs\FLOW-ON!"
} else {
    "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\FLOW-ON!"
}

if (-not (Test-Path $startMenuDir)) {
    New-Item -ItemType Directory -Force -Path $startMenuDir | Out-Null
}

$WshShell = New-Object -ComObject WScript.Shell
$shortcut = $WshShell.CreateShortcut("$startMenuDir\FLOW-ON!.lnk")
$shortcut.TargetPath = "$TargetDir\flow-on.exe"
$shortcut.WorkingDirectory = $TargetDir
$shortcut.IconLocation = "$TargetDir\flow-on.exe,0"
$shortcut.Description = "FLOW-ON! Voice Dictation"
$shortcut.Save()

# Desktop shortcut (optional)
$createDesktopShortcut = Read-Host "Create desktop shortcut? (Y/n)"
if ($createDesktopShortcut -ne 'n') {
    $desktopShortcut = $WshShell.CreateShortcut("$env:USERPROFILE\Desktop\FLOW-ON!.lnk")
    $desktopShortcut.TargetPath = "$TargetDir\flow-on.exe"
    $desktopShortcut.WorkingDirectory = $TargetDir
    $desktopShortcut.IconLocation = "$TargetDir\flow-on.exe,0"
    $desktopShortcut.Save()
    Write-Success "Desktop shortcut created"
}

Write-Success "Start Menu shortcut created"

# ------------------------------------------------------------------------------
# Start with Windows (optional)
# ------------------------------------------------------------------------------
$startWithWindows = Read-Host "Start FLOW-ON! automatically with Windows? (Y/n)"
if ($startWithWindows -ne 'n') {
    $regPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
    Set-ItemProperty -Path $regPath -Name "FLOW-ON" -Value "`"$TargetDir\flow-on.exe`"" -ErrorAction SilentlyContinue
    Write-Success "Added to startup programs"
}

# ------------------------------------------------------------------------------
# Launch Application
# ------------------------------------------------------------------------------
Write-Header "Installation Complete!"
Write-Host "FLOW-ON! has been installed to: $TargetDir" -ForegroundColor Green
Write-Host "`nUsage:" -ForegroundColor Cyan
Write-Host "  Press Alt+V to start recording" -ForegroundColor White
Write-Host "  Release Alt+V to transcribe and inject text" -ForegroundColor White
Write-Host "  Right-click the tray icon for options" -ForegroundColor White

$launchNow = Read-Host "`nLaunch FLOW-ON! now? (Y/n)"
if ($launchNow -ne 'n') {
    Start-Process "$TargetDir\flow-on.exe"
    Write-Success "FLOW-ON! launched!"
} else {
    Write-Host "You can start FLOW-ON! later from the Start Menu." -ForegroundColor Yellow
}

Write-Host "Thank you for installing FLOW-ON!" -ForegroundColor Cyan
