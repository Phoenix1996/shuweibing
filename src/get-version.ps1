$ErrorActionPreference='Stop'
$version=(Get-Content (Join-Path $PSScriptRoot '..\VERSION') -Raw).Trim()
if($version -notmatch '^\d+\.\d+\.\d+$'){throw 'VERSION 必须为数字形式的 major.minor.patch'}
$version
