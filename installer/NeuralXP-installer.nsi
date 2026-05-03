; NeuralXP Installer
; Built by Panmox (panmox.org)
; XP-compatible NSIS installer

!define APP_NAME "NeuralXP"
!define APP_VERSION "1.0.2"
!define APP_PUBLISHER "Panmox"
!define APP_URL "https://panmox.org"
!define APP_EXE "NeuralXP.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\NeuralXP"

; XP-compatible (don't use modern UI 2 with Vista+ features)
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "WinVer.nsh"

; --- Installer settings ---
Name "${APP_NAME}"
OutFile "NeuralXP-Setup-${APP_VERSION}.exe"
InstallDir "$PROGRAMFILES\NeuralXP"
InstallDirRegKey HKLM "Software\Panmox\NeuralXP" "InstallDir"
RequestExecutionLevel admin
ShowInstDetails show
ShowUnInstDetails show

; XP minimum
!define MIN_WIN_VER "5.1"

; --- Compression ---
SetCompressor /SOLID lzma
SetCompressorDictSize 32

; --- Branding ---
BrandingText "NeuralXP - Built by Panmox (panmox.org)"
VIProductVersion "1.0.0.0"
VIAddVersionKey "ProductName" "NeuralXP"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey "FileDescription" "NeuralXP Offline AI Installer"
VIAddVersionKey "LegalCopyright" "(c) 2026 Panmox"
VIAddVersionKey "FileVersion" "${APP_VERSION}"

; --- Modern UI configuration ---
!define MUI_ABORTWARNING
!define MUI_ICON "neuralxp.ico"
!define MUI_UNICON "neuralxp.ico"

; --- Pages ---
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "licenses\GPL-3.0.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch NeuralXP now"
!define MUI_FINISHPAGE_LINK "Visit panmox.org"
!define MUI_FINISHPAGE_LINK_LOCATION "https://panmox.org"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

; --- Sections ---

Section "NeuralXP (required)" SecMain
    SectionIn RO  ; required, can't deselect

    SetOutPath "$INSTDIR"

    ; Main executables
    File "..\deploy\NeuralXP.exe"
    File "..\deploy\llama-simple-chat-headless.exe"
    File "..\deploy\llama-simple-chat.exe"
    File "..\deploy\llama-simple.exe"

    ; Batch files
    File "..\deploy\run-on-xp.bat"
    File "..\deploy\chat-xp.bat"

    ; Model
    SetOutPath "$INSTDIR\models"
    File "..\deploy\models\qwen2.5-0.5b-instruct-q2_k.gguf"

    ; License files (required for legal compliance)
    SetOutPath "$INSTDIR\licenses"
    File "licenses\README.txt"
    File "licenses\GPL-3.0.txt"
    File "licenses\llama.cpp-LICENSE.txt"
    File "licenses\cpp-httplib-LICENSE.txt"
    File "licenses\Qwen2.5-LICENSE.txt"
    File "licenses\Apache-2.0.txt"
    File "licenses\GCC-Runtime-Exception.txt"

    ; Create chats folder
    SetOutPath "$INSTDIR\chats"

    ; --- Registry ---
    SetOutPath "$INSTDIR"
    WriteRegStr HKLM "Software\Panmox\NeuralXP" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\Panmox\NeuralXP" "Version" "${APP_VERSION}"

    ; --- Add/Remove Programs ---
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "${UNINST_KEY}" "URLInfoAbout" "${APP_URL}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1

    ; Estimate install size in KB
    WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" 425000

    ; --- Uninstaller ---
    WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Start Menu Shortcuts" SecStartMenu
    CreateDirectory "$SMPROGRAMS\NeuralXP"
    CreateShortCut "$SMPROGRAMS\NeuralXP\NeuralXP.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
    CreateShortCut "$SMPROGRAMS\NeuralXP\NeuralXP (Command Line).lnk" "$INSTDIR\chat-xp.bat" "" "$INSTDIR\${APP_EXE}" 0
    CreateShortCut "$SMPROGRAMS\NeuralXP\License Information.lnk" "$INSTDIR\licenses\README.txt"
    CreateShortCut "$SMPROGRAMS\NeuralXP\Uninstall NeuralXP.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop Shortcut" SecDesktop
    CreateShortCut "$DESKTOP\NeuralXP.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
SectionEnd

; --- Section descriptions ---
LangString DESC_SecMain ${LANG_ENGLISH} "Main NeuralXP files (required, ~415MB including AI model)."
LangString DESC_SecStartMenu ${LANG_ENGLISH} "Add NeuralXP to the Start Menu."
LangString DESC_SecDesktop ${LANG_ENGLISH} "Create a shortcut on your Desktop."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_SecMain)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} $(DESC_SecStartMenu)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_SecDesktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; --- Uninstaller ---

Section "Uninstall"
    ; Files
    Delete "$INSTDIR\NeuralXP.exe"
    Delete "$INSTDIR\llama-simple-chat-headless.exe"
    Delete "$INSTDIR\llama-simple-chat.exe"
    Delete "$INSTDIR\llama-simple.exe"
    Delete "$INSTDIR\run-on-xp.bat"
    Delete "$INSTDIR\chat-xp.bat"
    Delete "$INSTDIR\Uninstall.exe"

    ; Model
    Delete "$INSTDIR\models\qwen2.5-0.5b-instruct-q2_k.gguf"
    RMDir "$INSTDIR\models"

    ; License files
    Delete "$INSTDIR\licenses\README.txt"
    Delete "$INSTDIR\licenses\GPL-3.0.txt"
    Delete "$INSTDIR\licenses\llama.cpp-LICENSE.txt"
    Delete "$INSTDIR\licenses\cpp-httplib-LICENSE.txt"
    Delete "$INSTDIR\licenses\Qwen2.5-LICENSE.txt"
    Delete "$INSTDIR\licenses\Apache-2.0.txt"
    Delete "$INSTDIR\licenses\GCC-Runtime-Exception.txt"
    RMDir "$INSTDIR\licenses"

    ; Ask about chats folder (user data)
    MessageBox MB_YESNO|MB_ICONQUESTION "Delete saved chat files too?" IDNO SkipChats
        RMDir /r "$INSTDIR\chats"
    SkipChats:

    ; Try to remove install dir (only if empty)
    RMDir "$INSTDIR"

    ; Shortcuts
    Delete "$SMPROGRAMS\NeuralXP\NeuralXP.lnk"
    Delete "$SMPROGRAMS\NeuralXP\NeuralXP (Command Line).lnk"
    Delete "$SMPROGRAMS\NeuralXP\License Information.lnk"
    Delete "$SMPROGRAMS\NeuralXP\Uninstall NeuralXP.lnk"
    RMDir "$SMPROGRAMS\NeuralXP"
    Delete "$DESKTOP\NeuralXP.lnk"

    ; Registry cleanup
    DeleteRegKey HKLM "${UNINST_KEY}"
    DeleteRegKey HKLM "Software\Panmox\NeuralXP"
    DeleteRegKey /ifempty HKLM "Software\Panmox"
SectionEnd

; --- Init function: check Windows version ---
Function .onInit
    ; Check Windows version (XP = 5.1, anything older fails)
    ${If} ${AtMostWin2000}
        MessageBox MB_OK|MB_ICONSTOP "NeuralXP requires Windows XP or later."
        Abort
    ${EndIf}
FunctionEnd
