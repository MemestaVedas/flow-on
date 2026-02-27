FLOW-ON! Assets
===============

This directory holds assets required to build and ship FLOW-ON!.

Required files (not committed — generate before shipping)
---------------------------------------------------------
  app_icon.ico          — 256×256 + 128 + 64 + 48 + 32 + 16 px multi-size ICO
                          Used by the installer, taskbar, and Add/Remove Programs.

  tray_idle.ico         — 16×16 and 32×32 (dark background, white waveform / mic)
  tray_recording.ico    — 16×16 and 32×32 (red dot + "recording" glyph)
                          NOTE: currently flow-on.ico and small.ico (project root)
                          are used as temporary placeholders.  Replace them before
                          shipping.

  installer_banner.bmp  — 164 × 314 px, 24-bit BMP, shown on the NSIS wizard left
                          panel.  Dark background with the FLOW-ON! logo looks best.

Generating placeholder icons with PowerShell + .NET
----------------------------------------------------
Run the generate_icons.ps1 script in this directory (requires .NET 6+):

    pwsh assets\generate_icons.ps1

That script produces solid-colour placeholder ICO files so the project builds
without any graphical assets.  Replace them with real artwork before distribution.

Tools for final icons
---------------------
  ImageMagick:  magick convert logo.png -define icon:auto-resize=256,128,64,48,32,16 app_icon.ico
  IcoFX 3:      Professional GUI ICO editor (recommended)
  GIMP:         File → Export As → .ico, specify multiple sizes
