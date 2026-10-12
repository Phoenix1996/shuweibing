param([Parameter(Mandatory=$true)][string]$Zig)
$ErrorActionPreference='Stop'
$Zig=(Resolve-Path -LiteralPath $Zig).Path
$version=& (Join-Path $PSScriptRoot 'get-version.ps1')
$output=Join-Path $PSScriptRoot "..\dist\$version\鼠卫兵.exe"
Push-Location $PSScriptRoot
try {
    & (Join-Path $PSScriptRoot 'make-icon.ps1')
    $env:ZIG_GLOBAL_CACHE_DIR=Join-Path $PSScriptRoot '..\.build\zig-cache'
    New-Item -ItemType Directory (Split-Path $output -Parent) -Force | Out-Null
    & $Zig rc /c 65001 /fo app.res resources.rc
    if($LASTEXITCODE -ne 0){throw '资源编译失败'}
    & $Zig cc -target x86_64-windows-gnu -std=c99 -Os -DYYJSON_DISABLE_WRITER=1 -DYYJSON_DISABLE_UTILS=1 -c third_party/yyjson/yyjson.c -o '../.build/yyjson.o'
    if($LASTEXITCODE -ne 0){throw 'JSON 解析库编译失败'}
    & $Zig c++ -target x86_64-windows-gnu -std=c++17 -Os -s -fno-exceptions -fno-rtti -municode '-Wl,--subsystem,windows' main.cpp updater.cpp audio.cpp '../.build/yyjson.o' app.res -lwinhttp -lbcrypt -lversion -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -ldwmapi -ladvapi32 -lgdi32 -luxtheme -o $output
    if($LASTEXITCODE -ne 0){throw 'C++ 编译失败'}
    $file=Get-Item -LiteralPath $output
    if($file.VersionInfo.ProductVersion -ne $version){throw '程序资源版本与 VERSION 不一致'}
    if($file.VersionInfo.CompanyName -ne 'Phoenix Feng'){throw '程序资源作者署名缺失'}
    $file | Select-Object FullName,Length
} finally {Pop-Location}
