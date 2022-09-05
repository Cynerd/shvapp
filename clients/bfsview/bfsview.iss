; Script generated by the Inno Setup Script Wizard.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING INNO SETUP SCRIPT FILES!

#ifndef VERSION
#define VERSION "0.0.1"
#endif

#define APP_NAME "BfsView"
#define APP_NAME_LOWER "bfsview"
#define COMPANY "elektroline"

#define QT_DIR "C:\Qt5\5.15.2\mingw81_64"
#define MINGW_DIR "C:\mingw-12\x86_64-12.1.0-release-posix-seh-rt_v10-rev3\mingw64"

#define BUILD_DIR "..\.."

[Setup]
AppName={#APP_NAME}
AppVerName={#APP_NAME} {#VERSION}
AppPublisher=Elektroline
AppPublisherURL=http://www.{#APP_NAME_LOWER}.cz
AppSupportURL=http://www.{#APP_NAME_LOWER}.cz
AppUpdatesURL=http://www.{#APP_NAME_LOWER}.cz
DefaultDirName=C:\{#APP_NAME}
DefaultGroupName={#APP_NAME}
OutputDir={#BUILD_DIR}\_inno\{#APP_NAME_LOWER}
OutputBaseFilename={#APP_NAME_LOWER}-{#COMPANY}-{#VERSION}-setup
SetupIconFile=.\{#APP_NAME_LOWER}.ico
Compression=lzma
SolidCompression=yes

[Languages]
;Name: english; MessagesFile: compiler:Default.isl
;Name: czech; MessagesFile: compiler:Czech.isl

[Tasks]
Name: desktopicon; Description: {cm:CreateDesktopIcon}; GroupDescription: {cm:AdditionalIcons}; Flags: unchecked
Name: quicklaunchicon; Description: {cm:CreateQuickLaunchIcon}; GroupDescription: {cm:AdditionalIcons}; Flags: unchecked

[Files]
Source: {#BUILD_DIR}\bin\{#APP_NAME_LOWER}.exe; DestDir: {app}; Flags: ignoreversion
Source: {#BUILD_DIR}\bin\libnecrolog.dll; DestDir: {app}; Flags: ignoreversion
;Source: {#BUILD_DIR}\bin\libshvchainpack-c.dll; DestDir: {app}; Flags: ignoreversion
Source: {#BUILD_DIR}\bin\libshvchainpack-cpp.dll; DestDir: {app}; Flags: ignoreversion
Source: {#BUILD_DIR}\bin\libshvcore.dll; DestDir: {app}; Flags: ignoreversion
Source: {#BUILD_DIR}\bin\libshvcoreqt.dll; DestDir: {app}; Flags: ignoreversion
Source: {#BUILD_DIR}\bin\libshviotqt.dll; DestDir: {app}; Flags: ignoreversion

; NOTE: Don't use "Flags: ignoreversion" on any shared system files

Source: {#QT_DIR}\bin\Qt5Core.dll; DestDir: {app}; Flags: ignoreversion
Source: {#QT_DIR}\bin\Qt5Gui.dll; DestDir: {app}; Flags: ignoreversion
Source: {#QT_DIR}\bin\Qt5Widgets.dll; DestDir: {app}; Flags: ignoreversion
Source: {#QT_DIR}\bin\Qt5Network.dll; DestDir: {app}; Flags: ignoreversion
Source: {#QT_DIR}\bin\Qt5Svg.dll; DestDir: {app}; Flags: ignoreversion
Source: {#QT_DIR}\bin\Qt5Xml.dll; DestDir: {app}; Flags: ignoreversion
;Source: {#QT_DIR}\bin\Qt5Multimedia.dll; DestDir: {app}; Flags: ignoreversion
;Source: {#QT_DIR}\bin\QT5multimediawidgets.dll; DestDir: {app}; Flags: ignoreversion
;Source: {#QT_DIR}\bin\Qt5OpenGL.dll; DestDir: {app}; Flags: ignoreversion

;Source: {#QT_DIR}\plugins\mediaservice\dsengine.dll; DestDir: {app}\mediaservice; Flags: ignoreversion
Source: {#QT_DIR}\plugins\platforms\qwindows.dll; DestDir: {app}\platforms; Flags: ignoreversion
;Source: {#QT_DIR}\plugins\audio\qtaudio_windows.dll; DestDir: {app}\audio; Flags: ignoreversion
Source: {#QT_DIR}\plugins\imageformats\qsvg.dll; DestDir: {app}\imageformats; Flags: ignoreversion

Source: {#MINGW_DIR}\bin\libgcc_s_seh-1.dll; DestDir: {app}; Flags: ignoreversion
Source: {#MINGW_DIR}\bin\libwinpthread-1.dll; DestDir: {app}; Flags: ignoreversion
Source: {#MINGW_DIR}\bin\libstdc++-6.dll; DestDir: {app}; Flags: ignoreversion

[Icons]
Name: {group}\{#APP_NAME}; Filename: {app}\{#APP_NAME_LOWER}.exe
Name: {group}\{cm:UninstallProgram,{#APP_NAME}}; Filename: {uninstallexe}
Name: {userdesktop}\{#APP_NAME}; Filename: {app}\{#APP_NAME_LOWER}.exe; Tasks: desktopicon
Name: {userappdata}\Microsoft\Internet Explorer\Quick Launch\{#APP_NAME}; Filename: {app}\{#APP_NAME_LOWER}.exe; Tasks: quicklaunchicon

[Run]
Filename: {app}\{#APP_NAME_LOWER}.exe; Description: {cm:LaunchProgram,{#APP_NAME}}; Flags: nowait postinstall
