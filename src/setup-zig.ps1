$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$toolDir=Join-Path $root '.build\toolchain'
$zip=Join-Path $toolDir 'zig-x86_64-windows-0.15.2.zip'
$expected='3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c'
New-Item -ItemType Directory $toolDir -Force | Out-Null
if(!(Test-Path -LiteralPath $zip)){
    Invoke-WebRequest 'https://ziglang.org/download/0.15.2/zig-x86_64-windows-0.15.2.zip' -OutFile $zip
}
if((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected){
    throw 'Zig 下载包 SHA256 不匹配；请移除该下载包后重试。'
}
Expand-Archive -LiteralPath $zip -DestinationPath $toolDir -Force
$zig=Join-Path $toolDir 'zig-x86_64-windows-0.15.2\zig.exe'
& $zig version
if($LASTEXITCODE -ne 0){throw 'Zig 无法运行'}
Write-Host "Zig: $zig"
