param([Parameter(Mandatory=$true)][string]$Zig,[switch]$CI)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$Zig=(Resolve-Path -LiteralPath $Zig).Path
$version=& (Join-Path $PSScriptRoot 'get-version.ps1')
$testDir=Join-Path $root ".build\tests-$version"
New-Item -ItemType Directory $testDir -Force | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root '.build\zig-cache'
& $Zig c++ -target x86_64-windows-gnu -std=c++17 -Os -s -municode '-Wl,--subsystem,windows' (Join-Path $PSScriptRoot 'dpi-probe.cpp') -lshell32 -o (Join-Path $root '.build\dpi-probe.exe')
if($LASTEXITCODE -ne 0){throw 'DPI 测试程序编译失败'}
$testArgs=@('--self-test')
if($CI){$testArgs+='--ci'}
$testArgs+=("`"{0}`"" -f $testDir)
$process=Start-Process -FilePath (Join-Path $root "dist\$version\鼠卫兵.exe") -ArgumentList $testArgs -WindowStyle Hidden -PassThru
if(!$process.WaitForExit(120000)){
    $process.Kill()
    throw '内置测试超过 120 秒，已结束该测试进程'
}
Get-Content (Join-Path $testDir 'test-results.txt')
if($process.ExitCode -ne 0){throw '内置测试失败'}
