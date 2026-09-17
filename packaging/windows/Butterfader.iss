; Windows installer for the Butterfader VST3.
; iscc /DAppVersion=1.0.0 /DSourceDir=<folder containing Butterfader.vst3> /DOutputDir=<dist> Butterfader.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{6B2C7D0E-4E1F-4B8A-9C3D-B7F1A2E5C904}
AppName=Butterfader
AppVersion={#AppVersion}
AppPublisher=Butterfader
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallDisplayName=Butterfader VST3
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename=Butterfader-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "{#SourceDir}\Butterfader.vst3\*"; DestDir: "{commoncf64}\VST3\Butterfader.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
FinishedLabel=Butterfader is installed.%n%nPremiere Pro: Preferences > Audio > Audio Plug-in Manager > Scan for Plug-ins.%nAudition: Effects > Audio Plug-in Manager > Scan for Plug-ins.
