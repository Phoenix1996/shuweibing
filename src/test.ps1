param([Parameter(Mandatory=$true)][string]$Zig)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$testDir=Join-Path $root '.build\tests-0.3'
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root '.build\zig-cache'
& $Zig c++ -target x86_64-windows-gnu -std=c++17 -Os -s -municode '-Wl,--subsystem,windows' (Join-Path $PSScriptRoot 'dpi-probe.cpp') -lshell32 -o (Join-Path $root '.build\dpi-probe.exe')
if($LASTEXITCODE -ne 0){throw 'DPI 测试程序编译失败'}
$process=Start-Process -FilePath (Join-Path $root 'dist\0.3\鼠卫兵.exe') -ArgumentList '--self-test',("`"{0}`"" -f $testDir) -WindowStyle Hidden -PassThru
$process.WaitForExit()
Get-Content (Join-Path $testDir 'test-results.txt')
if($process.ExitCode -ne 0){throw '内置测试失败'}
