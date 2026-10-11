param([Parameter(Mandatory=$true)][string]$Zig)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    & (Join-Path $PSScriptRoot 'make-icon.ps1')
    $env:ZIG_GLOBAL_CACHE_DIR=Join-Path $PSScriptRoot '..\.build\zig-cache'
    New-Item -ItemType Directory (Join-Path $PSScriptRoot '..\dist\0.2') -Force | Out-Null
    & $Zig rc /c 65001 /fo app.res resources.rc
    if($LASTEXITCODE -ne 0){throw '资源编译失败'}
    & $Zig c++ -target x86_64-windows-gnu -std=c++17 -Os -s -fno-exceptions -fno-rtti -municode '-Wl,--subsystem,windows' main.cpp app.res -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -ldwmapi -ladvapi32 -lgdi32 -luxtheme -o '..\dist\0.2\鼠卫兵.exe'
    if($LASTEXITCODE -ne 0){throw 'C++ 编译失败'}
    Get-Item '..\dist\0.2\鼠卫兵.exe' | Select-Object FullName,Length
} finally {Pop-Location}
