$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$version=& (Join-Path $PSScriptRoot 'get-version.ps1')
$stage=Join-Path $root "dist\$version"
$exe=Join-Path $stage '鼠卫兵.exe'
if(!(Test-Path -LiteralPath $exe)){throw '请先运行 build.ps1'}
$info=(Get-Item -LiteralPath $exe).VersionInfo
if($info.ProductVersion -ne $version -or $info.CompanyName -ne 'Phoenix Feng'){throw '版本或作者署名不匹配'}
foreach($name in @('README.md','LICENSE','NOTICE.md','CHANGELOG.md','验证记录.md')){
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $stage -Force
}
$assets=Join-Path $stage 'assets'
New-Item -ItemType Directory $assets -Force | Out-Null
foreach($name in @('mouse-guardian.png','图标说明.md')){
    Copy-Item -LiteralPath (Join-Path $root "assets\$name") -Destination $assets -Force
}
$utf8=[System.Text.UTF8Encoding]::new($false)
$exeHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText((Join-Path $stage 'SHA256SUMS.txt'),"$exeHash  鼠卫兵.exe`n",$utf8)
$output=Join-Path $root "dist\packages\$version"
New-Item -ItemType Directory $output -Force | Out-Null
$zipName="鼠卫兵-$version-windows-x64.zip"
$zip=Join-Path $output $zipName
# Explicit allowlist excludes obsolete files, settings and test fixtures from the ZIP.
$inputs=@('鼠卫兵.exe','README.md','LICENSE','NOTICE.md','CHANGELOG.md','验证记录.md','SHA256SUMS.txt','assets') | ForEach-Object {Join-Path $stage $_}
Compress-Archive -LiteralPath $inputs -DestinationPath $zip -Force
$zipHash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'),"$zipHash  $zipName`n",$utf8)
$report=Join-Path $root ".build\tests-$version\test-results.txt"
if(!(Test-Path -LiteralPath $report)){throw '缺少测试报告，请先运行 test.ps1'}
$result=Get-Content -LiteralPath $report | Where-Object {$_ -match '^RESULT '}
if(!$result -or (Select-String -LiteralPath $report -Pattern '^FAIL ')){throw '测试报告未通过，取消发布准备'}
$skips=Get-Content -LiteralPath $report | Where-Object {$_ -match '^SKIP '}
$notes=@("# 鼠卫兵 $version",'', '作者：**Phoenix Feng**','', 'Windows 10 / 11 x64 原生窗口管理工具。','', '## 下载','', "解压 ZIP 后运行鼠卫兵.exe。程序未签名。包内保留使用说明、MIT 许可证、作者及素材说明。",'', '## 验证', '', "$result")
if($skips){$notes+=@('','CI 环境跳过项：')+$skips}
$notes+=@('', '自测使用自有窗口和进程，不能替代真实游戏兼容性及账号环境验证。', '', '## 更新记录', '')+(Get-Content (Join-Path $root 'CHANGELOG.md'))
[System.IO.File]::WriteAllText((Join-Path $output 'RELEASE_NOTES.md'),($notes -join "`n")+"`n",$utf8)
Get-ChildItem -LiteralPath $output | Select-Object Name,Length
