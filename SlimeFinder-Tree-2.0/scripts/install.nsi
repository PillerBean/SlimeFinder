; SlimeFinder 安装脚本（NSIS 3.x）—— v3.5.0 首个正式版
; 用法：makensis install.nsi   （或用 scripts\build_installer.ps1 一步完成）
;
; 任务书要点（v3.5.0）：
;   * 全机器安装：默认 $PROGRAMFILES64\SlimeFinder，安装时可自选路径；需要 UAC（RequestExecutionLevel admin）
;   * 快捷方式：开始菜单（所有用户，必需）+ 桌面（当前用户，可选，见组件页）
;   * 依赖全部内置：Qt 运行时 / OpenCL 加载器 / VC++ 运行时（app-local，已在 dist 目录内）
;   * 带卸载程序，出现在「应用和功能」；卸载删除程序文件 + 快捷方式 + 注册表项 + 用户配置 + 缓存
;   * 旧版处理：发现旧 config.json / 旧 slime_cache.slimecache 时**改名保留**（*.legacy），
;     程序不会自动加载（它只认原名），用户可用程序内的「导入缓存」手动恢复

Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"

!define APP_NAME "史莱姆区块查找器"
!define APP_KEY "SlimeFinder"
!define APP_VERSION "3.11.2"
!define APP_EXE "SlimeFinder.exe"
!define APP_PUBLISHER "SlimeFinder"
!define SOURCE_DIR "..\dist\SlimeFinder"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_KEY}"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "..\dist\SlimeFinder-${APP_VERSION}-Setup.exe"
InstallDir "$PROGRAMFILES64\${APP_KEY}"
InstallDirRegKey HKLM "Software\${APP_KEY}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

; v3.11.2：安装语言选择（也会作为程序首次启动语言）
Page custom LanguageHintPageCreate LanguageHintPageLeave

; 页面：欢迎 → 选择安装目录 → 组件（桌面快捷方式可选） → 安装 → 完成
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "TradChinese"
!insertmacro MUI_LANGUAGE "English"

Function .onInit
    !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

LangString SEC_MAIN 2052 "主程序（必需）"
LangString SEC_DESKTOP 2052 "桌面快捷方式（可选）"
LangString SEC_MAIN 1028 "主程式（必要）"
LangString SEC_DESKTOP 1028 "桌面捷徑（選用）"
LangString SEC_MAIN 1033 "Main program (required)"
LangString SEC_DESKTOP 1033 "Desktop shortcut (optional)"

Function LanguageHintPageCreate
    nsDialogs::Create 1018
    Pop $0
    ${If} $LANGUAGE == 1028
        ${NSD_CreateLabel} 0 0 100% 72u "您選擇的安裝語言將作為程式首次啟動語言；之後可在「設定 → 一般 → 介面語言」修改。"
    ${ElseIf} $LANGUAGE == 1033
        ${NSD_CreateLabel} 0 0 100% 72u "The setup language will be used as the app's first-start language. You can change it later in Settings → General → Language."
    ${Else}
        ${NSD_CreateLabel} 0 0 100% 72u "您选择的安装语言将作为程序首次启动语言；之后可在「设置 → 通用 → 界面语言」修改。"
    ${EndIf}
    Pop $1
    nsDialogs::Show
FunctionEnd

Function LanguageHintPageLeave
FunctionEnd

Section "$(SEC_MAIN)" SecMain
    SectionIn RO
    SetOutPath "$INSTDIR"

    ; v3.10.12.10：自动更新以 /UPDATE=1 启动安装器时，保留用户设置与缓存（不按全新安装重置）
    ${GetParameters} $R0
    ${GetOptions} $R0 "/UPDATE=" $R1
    IfErrors 0 +2
    StrCpy $R1 "0"

    ${If} $R1 != "1"
        ; ---- 全新安装/旧版处理（必须在写文件之前：否则新 config.json 会覆盖掉旧文件）----
        ; 旧配置：改名保留（程序只读 config.json，改名后不会被自动加载，用默认设置启动）
        IfFileExists "$INSTDIR\config.json" 0 +2
            Rename "$INSTDIR\config.json" "$INSTDIR\config.json.legacy"
        ; 旧缓存（新扩展名）：改名保留，程序不会加载
        IfFileExists "$INSTDIR\slime_cache.slimecache" 0 +2
            Rename "$INSTDIR\slime_cache.slimecache" "$INSTDIR\slime_cache.slimecache.legacy"
        ; 旧缓存（更早的扩展名，一并处理）
        IfFileExists "$INSTDIR\slime_cache.cache" 0 +2
            Rename "$INSTDIR\slime_cache.cache" "$INSTDIR\slime_cache.cache.legacy"
    ${EndIf}

    ; ---- 程序本体 + 内置依赖（Qt 运行时 / OpenCL 加载器 / VC++ 运行时 / 平台插件）----
    File /r "${SOURCE_DIR}\*.*"

    ; v3.11.2：安装语言 = 程序首次启动语言（仅全新安装，更新时保留用户配置）
    ${If} $R1 != "1"
        ${If} $LANGUAGE == 1028
            CopyFiles /SILENT "$INSTDIR\config.zh-TW.json" "$INSTDIR\config.json"
        ${ElseIf} $LANGUAGE == 1033
            CopyFiles /SILENT "$INSTDIR\config.en.json" "$INSTDIR\config.json"
        ${Else}
            CopyFiles /SILENT "$INSTDIR\config.zh.json" "$INSTDIR\config.json"
        ${EndIf}
    ${EndIf}
    Delete "$INSTDIR\config.zh.json"
    Delete "$INSTDIR\config.zh-TW.json"
    Delete "$INSTDIR\config.en.json"

    ; ---- 开始菜单 + 桌面（全机器安装 ⇒ 用「所有用户」的 shell 变量上下文）----
    ;   NSIS 没有 $COMMONPROGRAMS 常量：设置 SetShellVarContext all 后，
    ;   $SMPROGRAMS / $DESKTOP 就分别指向
    ;   C:\ProgramData\Microsoft\Windows\Start Menu\Programs 与 C:\Users\Public\Desktop
    SetShellVarContext all
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
    CreateShortCut "$SMPROGRAMS\${APP_NAME}\卸载 ${APP_NAME}.lnk" "$INSTDIR\Uninstall.exe"

    ; ---- 注册表：安装位置 + 卸载信息（「应用和功能」入口）----
    ; v3.5.0：NSIS 生成的是 32 位安装器，默认会把 HKLM 写进 WOW6432Node；
    ;   本程序是 64 位应用 ⇒ 显式使用 64 位注册表视图（安装 + 卸载两侧都要设）。
    SetRegView 64
    WriteRegStr HKLM "Software\${APP_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\${APP_KEY}" "Version" "${APP_VERSION}"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
    WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "$(SEC_DESKTOP)" SecDesktop
    CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
!insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "程序本体与全部内置依赖（Qt 运行时、OpenCL 加载器、VC++ 运行时）。"
!insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "在桌面创建快捷方式（可跳过）。"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
    SetRegView 64       ; v3.5.0：与安装时一致，删掉 64 位视图里的键
    SetShellVarContext all ; v3.5.0：快捷方式建在「所有用户」位置，删除时也要用同一上下文
    ; 快捷方式
    Delete "$DESKTOP\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\卸载 ${APP_NAME}.lnk"
    RMDir "$SMPROGRAMS\${APP_NAME}"
    ; 注册表项（卸载信息 + 安装信息 + 文件关联）
    DeleteRegKey HKLM "${UNINST_KEY}"
    DeleteRegKey HKLM "Software\${APP_KEY}"
    DeleteRegKey HKLM "Software\Classes\SlimeFinder.CacheFile"
    DeleteRegKey HKLM "Software\Classes\.cache"
    ; 程序文件 + 用户配置 + 缓存 + 日志 + 崩溃转储（整目录删除）
    SetOutPath "$TEMP"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir /r "$INSTDIR"
SectionEnd
