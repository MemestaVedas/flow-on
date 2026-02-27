; FLOW-ON! NSIS Installer Script
; Build command (from project root, after cmake --build build --config Release):
;   makensis installer\flow-on.nsi
; Output: FLOW-ON-Setup.exe in project root (~95 MB with model + runtime)

; -----------------------------------------------------------------------
!define APP_NAME      "FLOW-ON!"
!define APP_VERSION   "1.0.0"
!define APP_PUBLISHER "Your Name"
!define APP_EXE       "flow-on.exe"
!define INSTALL_DIR   "$PROGRAMFILES64\FLOW-ON"
!define REG_UNINSTALL "Software\Microsoft\Windows\CurrentVersion\Uninstall\FLOW-ON"
!define REG_KEY       "Software\FLOW-ON"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "..\FLOW-ON-Setup.exe"
InstallDir "${INSTALL_DIR}"
InstallDirRegKey HKLM "${REG_KEY}" "InstallPath"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

; -----------------------------------------------------------------------
; Modern UI 2
; -----------------------------------------------------------------------
!include "MUI2.nsh"
!include "FileFunc.nsh"

!define MUI_ICON   "..\assets\app_icon.ico"
!define MUI_UNICON "..\assets\app_icon.ico"

; Uncomment and set MUI_WELCOMEFINISHPAGE_BITMAP for the 164x314 sidebar image:
; !define MUI_WELCOMEFINISHPAGE_BITMAP "..\assets\installer_banner.bmp"

!define MUI_FINISHPAGE_RUN          "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT     "Start FLOW-ON! now"
!define MUI_ABORTWARNING

; Installer pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; -----------------------------------------------------------------------
; VERSION INFORMATION (Add/Remove Programs shows this)
; -----------------------------------------------------------------------
VIProductVersion "1.0.0.0"
VIAddVersionKey "ProductName"      "${APP_NAME}"
VIAddVersionKey "ProductVersion"   "${APP_VERSION}"
VIAddVersionKey "CompanyName"      "${APP_PUBLISHER}"
VIAddVersionKey "FileDescription"  "FLOW-ON! Installer"
VIAddVersionKey "FileVersion"      "${APP_VERSION}"
VIAddVersionKey "LegalCopyright"   "Copyright 2025 ${APP_PUBLISHER}"

; -----------------------------------------------------------------------
; Section 1 — Windows App SDK Runtime (required for WinUI 3 dashboard)
; Install silently FIRST; the runtime checks internally if already present.
; -----------------------------------------------------------------------
Section "Windows App SDK Runtime" SecRuntime
    SectionIn RO   ; mandatory, cannot be de-selected
    SetOutPath "$INSTDIR"
    File "..\redist\WindowsAppRuntimeInstall-x64.exe"
    DetailPrint "Installing Windows App SDK runtime (first-time only)…"
    ExecWait '"$INSTDIR\WindowsAppRuntimeInstall-x64.exe" --quiet' $0
    Delete "$INSTDIR\WindowsAppRuntimeInstall-x64.exe"
    ${If} $0 != 0
        DetailPrint "Warning: Windows App SDK installer returned $0 (may already be installed)"
    ${EndIf}
SectionEnd

; -----------------------------------------------------------------------
; Section 2 — Main application
; -----------------------------------------------------------------------
Section "Main Application" SecMain
    SectionIn RO   ; mandatory

    SetOutPath "$INSTDIR"
    File "..\build\Release\${APP_EXE}"

    ; Whisper model (~75 MB) — largest single file
    CreateDirectory "$INSTDIR\models"
    SetOutPath "$INSTDIR\models"
    File "..\models\ggml-tiny.en.bin"

    ; Tray icons
    CreateDirectory "$INSTDIR\assets"
    SetOutPath "$INSTDIR\assets"
    File /nonfatal "..\assets\*.ico"

    ; ---- Shortcuts ----
    CreateDirectory "$SMPROGRAMS\FLOW-ON!"
    CreateShortcut "$SMPROGRAMS\FLOW-ON!\FLOW-ON!.lnk"    "$INSTDIR\${APP_EXE}" "" "$INSTDIR\assets\app_icon.ico"
    CreateShortcut "$SMPROGRAMS\FLOW-ON!\Uninstall.lnk"   "$INSTDIR\Uninstall.exe"
    CreateShortcut "$DESKTOP\FLOW-ON!.lnk"                 "$INSTDIR\${APP_EXE}" "" "$INSTDIR\assets\app_icon.ico"

    ; ---- Start with Windows (HKCU — no UAC at runtime) ----
    WriteRegStr HKCU \
        "Software\Microsoft\Windows\CurrentVersion\Run" \
        "FLOW-ON" '"$INSTDIR\${APP_EXE}"'

    ; ---- Add/Remove Programs entry ----
    WriteRegStr HKLM "${REG_UNINSTALL}" "DisplayName"          "${APP_NAME}"
    WriteRegStr HKLM "${REG_UNINSTALL}" "UninstallString"       '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "${REG_UNINSTALL}" "QuietUninstallString"  '"$INSTDIR\Uninstall.exe" /S'
    WriteRegStr HKLM "${REG_UNINSTALL}" "DisplayVersion"        "${APP_VERSION}"
    WriteRegStr HKLM "${REG_UNINSTALL}" "Publisher"             "${APP_PUBLISHER}"
    WriteRegStr HKLM "${REG_UNINSTALL}" "DisplayIcon"           "$INSTDIR\assets\app_icon.ico"
    WriteRegStr HKLM "${REG_UNINSTALL}" "InstallLocation"       "$INSTDIR"
    WriteRegStr HKLM "${REG_UNINSTALL}" "URLInfoAbout"          "https://github.com/YourHandle/FLOW-ON"
    WriteRegDWORD HKLM "${REG_UNINSTALL}" "NoModify"            1
    WriteRegDWORD HKLM "${REG_UNINSTALL}" "NoRepair"            1
    WriteRegDWORD HKLM "${REG_UNINSTALL}" "EstimatedSize"       98000   ; KB (~96 MB installed)

    WriteRegStr HKLM "${REG_KEY}" "InstallPath" "$INSTDIR"

    WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

; -----------------------------------------------------------------------
; Uninstaller
; -----------------------------------------------------------------------
Section "Uninstall"
    ; Kill any running instance gracefully first, then force-kill
    ExecWait 'taskkill /IM ${APP_EXE}' $0
    Sleep 800
    ExecWait 'taskkill /F /IM ${APP_EXE}'
    Sleep 400

    ; Remove installed files
    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\models\ggml-tiny.en.bin"
    Delete "$INSTDIR\assets\*.ico"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir  "$INSTDIR\models"
    RMDir  "$INSTDIR\assets"
    RMDir  "$INSTDIR"

    ; Remove shortcuts
    Delete "$SMPROGRAMS\FLOW-ON!\FLOW-ON!.lnk"
    Delete "$SMPROGRAMS\FLOW-ON!\Uninstall.lnk"
    RMDir  "$SMPROGRAMS\FLOW-ON!"
    Delete "$DESKTOP\FLOW-ON!.lnk"

    ; Remove registry entries
    DeleteRegValue HKCU \
        "Software\Microsoft\Windows\CurrentVersion\Run" "FLOW-ON"
    DeleteRegKey HKLM "${REG_UNINSTALL}"
    DeleteRegKey HKLM "${REG_KEY}"
SectionEnd
