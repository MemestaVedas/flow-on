# Getting Started

<cite>
**Referenced Files in This Document**
- [README.md](file://README.md)
- [CMakeLists.txt](file://CMakeLists.txt)
- [build.ps1](file://build.ps1)
- [install.ps1](file://install.ps1)
- [setup-github.ps1](file://setup-github.ps1)
- [installer/flow-on.nsi](file://installer/flow-on.nsi)
- [src/main.cpp](file://src/main.cpp)
- [src/config_manager.cpp](file://src/config_manager.cpp)
- [assets/settings.default.json](file://assets/settings.default.json)
- [CONTRIBUTING.md](file://CONTRIBUTING.md)
</cite>

## Update Summary
**Changes Made**
- Added comprehensive PowerShell installer script documentation with one-click installation capabilities
- Updated build requirements to Visual Studio 2022, CMake 3.20+, and Git
- Streamlined deployment process with automatic CUDA detection and optional pre-built releases
- Enhanced first-time setup procedures with improved model downloading and settings creation
- Added PowerShell installer script usage examples and troubleshooting guidance

## Table of Contents
1. [Introduction](#introduction)
2. [Installation Methods](#installation-methods)
3. [Prerequisites and Environment](#prerequisites-and-environment)
4. [Build and Compilation](#build-and-compilation)
5. [First-Time Setup](#first-time-setup)
6. [Basic Usage](#basic-usage)
7. [Advanced Configuration](#advanced-configuration)
8. [Troubleshooting Guide](#troubleshooting-guide)
9. [Verification Steps](#verification-steps)
10. [Conclusion](#conclusion)

## Introduction
This Getting Started guide helps you quickly install and begin using Flow-On from source or via the streamlined installer. You will learn multiple installation approaches:
- One-click PowerShell installer with automatic CUDA detection
- Traditional source compilation with Visual Studio 2022
- Automated build processes with PowerShell orchestration
- Complete first-time setup including model downloading and configuration

## Installation Methods

### Method 1: One-Click PowerShell Installer (Recommended)
The simplest way to install Flow-On is using the PowerShell installer script, which provides automatic dependency detection and installation.

**Requirements:**
- Windows 10/11 x64
- PowerShell 5.0+ (built-in with Windows)
- Internet connection for model and dependency downloads

**Installation Steps:**
1. Open PowerShell as Administrator (for system-wide installation) or regular user (for user-only installation)
2. Run the installer script with desired options:

```powershell
# System-wide installation with CUDA support
.\install.ps1 -SystemWide -CUDA

# User-only installation with pre-built binaries (no build required)
.\install.ps1 -NoBuild

# Custom installation directory
.\install.ps1 -InstallDir "C:\Custom\Path"
```

**Features of the PowerShell Installer:**
- Automatic Visual Studio 2022 detection and validation
- CMake 3.20+ and Git dependency checking
- Pre-built release download option (skips compilation)
- Automatic CUDA detection and GPU acceleration enablement
- Shortcut creation for Start Menu and Desktop
- Windows startup integration option

### Method 2: Traditional Source Installation
For developers who prefer building from source or need custom configurations:

**Prerequisites:**
- Windows 10/11 x64
- Visual Studio 2022 (v143 toolset) or Build Tools
- CMake 3.20+
- Git for repository cloning

**Build Steps:**
```powershell
# Clone repository with submodules
git clone --recursive https://github.com/MemestaVedas/flow-on
cd flow-on

# Build with Release configuration
.\build.ps1 -Release

# Enable CUDA support (if available)
.\build.ps1 -Release -CUDA

# Generate NSIS installer (requires makensis)
.\build.ps1 -Installer
```

### Method 3: From Pre-built Installer
Skip compilation entirely and use the official installer:

```cmd
flow-on-installer.exe
```

This installs to `Program Files\FLOW-ON`, creates Start Menu shortcuts, and registers with Add/Remove Programs.

**Section sources**
- [install.ps1](file://install.ps1#L1-L303)
- [build.ps1](file://build.ps1#L1-L89)
- [README.md](file://README.md#L15-L67)

## Prerequisites and Environment

### System Requirements
- **Operating System**: Windows 10/11 x64 (Windows 10 version 2004 or later required)
- **Visual Studio**: 2022 (v143 toolset) - Community, Professional, Enterprise, or Build Tools
- **Build Tools**: CMake 3.20+ and Git for source compilation
- **PowerShell**: 5.0+ for automated installation and build scripts

### Dependency Validation
The PowerShell installer automatically validates your environment:

```powershell
# Check Windows version
$osInfo = Get-CimInstance Win32_OperatingSystem
if ($osInfo.Version -lt "10.0.19041") {
    Write-Error "Windows 10 version 2004 or later is required"
}

# Check Visual Studio 2022 installation
$vsPath = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
$hasVS = (Test-Path $vsPath) -or (Test-Path $vsEnterprise) -or (Test-Path $vsProfessional) -or (Test-Path $vsBuildTools)
```

**Section sources**
- [install.ps1](file://install.ps1#L93-L147)
- [README.md](file://README.md#L17-L21)

## Build and Compilation

### Automated Build Process
The PowerShell build script (`build.ps1`) automates the entire compilation pipeline:

**Build Options:**
```powershell
# Release build with optimizations
.\build.ps1 -Release

# Enable NVIDIA CUDA acceleration
.\build.ps1 -Release -CUDA

# Generate NSIS installer
.\build.ps1 -Installer

# Combine options
.\build.ps1 -Release -CUDA -Installer
```

**Build Pipeline:**
1. **Model Download**: Automatically downloads Whisper base.en model (~150 MB)
2. **Icon Generation**: Creates placeholder icons if missing
3. **CMake Configuration**: Sets up Visual Studio 2022 generator with x64 platform
4. **Compilation**: Builds Release configuration with aggressive optimizations
5. **Installer Creation**: Optional NSIS installer generation

### Manual CMake Workflow
For advanced users preferring manual control:

```cmake
# Configure with Visual Studio 2022 generator
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build Release configuration
cmake --build build --config Release --parallel

# Launch the application
.\build\Release\flow-on.exe
```

**Section sources**
- [build.ps1](file://build.ps1#L1-L89)
- [CMakeLists.txt](file://CMakeLists.txt#L1-L142)

## First-Time Setup

### Automatic Model Downloading
On first run, Flow-On automatically handles model management:

```powershell
# Model download process (automated)
if (-not (Test-Path $modelPath)) {
    Write-Host "[1/4] Downloading Whisper base.en model (~150 MB)..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Force models | Out-Null
    Invoke-WebRequest `
        -Uri "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin" `
        -OutFile $modelPath `
        -UseBasicParsing
}
```

### Settings Configuration
Flow-On creates a comprehensive settings.json in `%APPDATA%\FLOW-ON\`:

```json
{
  "hotkey": "Alt+V",
  "mode": "auto",
  "model": "tiny.en",
  "use_gpu": true,
  "start_with_windows": true,
  "snippets": {
    "insert email": "you@yourdomain.com",
    "insert boilerplate": "import React, { useState, useEffect } from 'react';\n\nexport default function Component() {\n  return <div></div>;\n}",
    "insert todo": "// TODO: ",
    "insert fixme": "// FIXME: ",
    "insert date": "2026-02-27",
    "insert github": "https://github.com/YourHandle"
  }
}
```

### System Tray Integration
Upon successful setup, Flow-On displays:
- System tray icon with dynamic tooltips
- Hotkey registration (Alt+V or Alt+Shift+V fallback)
- Dashboard access via right-click context menu
- Optional Windows startup integration

**Section sources**
- [build.ps1](file://build.ps1#L21-L32)
- [src/config_manager.cpp](file://src/config_manager.cpp#L24-L58)
- [assets/settings.default.json](file://assets/settings.default.json#L1-L16)
- [src/main.cpp](file://src/main.cpp#L162-L180)

## Basic Usage

### Hotkey Functionality
- **Primary Hotkey**: Alt+V to start recording, release to transcribe
- **Fallback Hotkey**: If Alt+V is taken, automatically uses Alt+Shift+V
- **Visual Feedback**: 
  - Blue waveform bars during recording
  - Spinner arc during transcription
  - Green flash on successful injection
  - Red flash on error

### Modes of Operation
- **AUTO Mode**: Detects editors and applies coding transforms
- **NORMAL Mode**: Cleans fillers and fixes punctuation
- **CODING Mode**: CamelCase/snake_case transforms with tech vocabulary

### Dashboard Access
Right-click the system tray icon to access:
- History panel with all recent transcriptions
- Settings configuration
- Autostart toggle
- GPU usage control

**Section sources**
- [README.md](file://README.md#L278-L304)
- [src/main.cpp](file://src/main.cpp#L185-L203)
- [src/main.cpp](file://src/main.cpp#L244-L274)

## Advanced Configuration

### CUDA Acceleration
Enable NVIDIA GPU acceleration for significantly faster transcription:

```powershell
# Automatic detection and enablement
.\install.ps1 -CUDA

# Or manual build with CUDA
.\build.ps1 -Release -CUDA
```

**Requirements:**
- NVIDIA GPU with CUDA support
- Compatible driver version
- Automatic detection in installer script

### Custom Installation Paths
```powershell
# System-wide installation
.\install.ps1 -SystemWide

# User-only installation
.\install.ps1

# Custom directory
.\install.ps1 -InstallDir "C:\Custom\Path"
```

### Pre-built Releases
Skip compilation entirely:
```powershell
# Download and install pre-built binaries
.\install.ps1 -NoBuild
```

**Section sources**
- [install.ps1](file://install.ps1#L108-L147)
- [build.ps1](file://build.ps1#L49-L52)

## Troubleshooting Guide

### Common Installation Issues

**PowerShell Installer Problems:**
- **Administrator Privileges Required**: System-wide installations need Admin rights
- **Visual Studio 2022 Missing**: Install Visual Studio 2022 Community/Professional/Enterprise
- **CMake Not Found**: Download CMake 3.20+ from cmake.org
- **Git Not Available**: Install Git for Windows

**Build Failures:**
- **Visual Studio 2022 Toolset Missing**: Ensure v143 toolset is installed
- **CMake Version Too Old**: Upgrade to CMake 3.20+
- **Missing Dependencies**: Install required Visual C++ build tools

**Runtime Issues:**
- **Hotkey Conflicts**: Alternative hotkey (Alt+Shift+V) automatically configured
- **Audio Device Problems**: Check Windows sound settings and microphone permissions
- **Model Download Failures**: Verify internet connection and disk space

### Verification Checklist
After installation, verify:
- Executable located in installation directory
- Settings.json created in `%APPDATA%\FLOW-ON\`
- System tray icon appears with tooltip
- Hotkey responds (Alt+V or fallback)
- First transcription completes successfully

**Section sources**
- [install.ps1](file://install.ps1#L101-L147)
- [src/main.cpp](file://src/main.cpp#L162-L180)
- [README.md](file://README.md#L326-L346)

## Verification Steps

### Build Verification
```powershell
# Confirm build success
if (Test-Path "build\Release\flow-on.exe") {
    Write-Host "Build successful: flow-on.exe found" -ForegroundColor Green
} else {
    Write-Error "Build failed: executable not found"
}
```

### Runtime Verification
1. **Launch Application**: Start Flow-On from Start Menu or installation directory
2. **Check Tray Icon**: Verify system tray icon appears with tooltip
3. **Test Hotkey**: Press Alt+V to start recording, release to transcribe
4. **Verify Settings**: Check `%APPDATA%\FLOW-ON\settings.json` exists
5. **Test Dashboard**: Right-click tray icon and open Dashboard

### Performance Testing
- **First Transcription**: Expect ~12-18 seconds for 30-second audio
- **GPU Acceleration**: Verify CUDA-enabled builds show improved performance
- **Memory Usage**: Monitor ~400 MB memory usage with model loaded

**Section sources**
- [README.md](file://README.md#L305-L325)
- [src/main.cpp](file://src/main.cpp#L408-L494)

## Conclusion

You now have multiple pathways to install and use Flow-On effectively:

**For End Users**: Use the PowerShell installer for the fastest setup experience with automatic dependency management and CUDA detection.

**For Developers**: Utilize the build scripts and manual CMake workflow for custom configurations and development iterations.

**For Production**: Leverage the NSIS installer for enterprise deployments with standardized configurations.

The streamlined installation process, combined with robust error handling and automatic dependency management, makes Flow-On accessible to users of all technical levels while providing the flexibility needed for advanced customization.