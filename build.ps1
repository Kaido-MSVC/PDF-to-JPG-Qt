param([string]$QtRoot='D:\qt\6.12.0\msvc2022_64', [switch]$Testing)
$ErrorActionPreference='Stop'
$projectRoot=$PSScriptRoot
$buildName=if($Testing){'build-test'}else{'build'}
$distName=if($Testing){'dist-test'}else{'dist'}
$testOption=if($Testing){'ON'}else{'OFF'}
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw '找不到 MSVC x64 工具鏈'}
$vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$cmake='D:\qt\Tools\CMake_64\bin\cmake.exe'
$ninja='D:\qt\Tools\Ninja\ninja.exe'
$cmdFile=Join-Path $projectRoot 'work\build.cmd'
New-Item -ItemType Directory -Force (Split-Path $cmdFile) | Out-Null
@"
@echo off
set VSLANG=1033
call "$vcvars"
if errorlevel 1 exit /b 1
"$cmake" -S "$projectRoot" -B "$projectRoot\$buildName" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPDF2JPG_TESTING=$testOption -DCMAKE_PREFIX_PATH="$QtRoot" -DCMAKE_MAKE_PROGRAM="$ninja"
if errorlevel 1 exit /b 1
"$cmake" --build "$projectRoot\$buildName" --parallel 4
if errorlevel 1 exit /b 1
if not exist "$projectRoot\$distName" mkdir "$projectRoot\$distName"
copy /y "$projectRoot\$buildName\PDF-to-JPG_QT.exe" "$projectRoot\$distName\PDF-to-JPG_QT.exe" >nul
"$QtRoot\bin\windeployqt.exe" --release --no-compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler "$projectRoot\$distName\PDF-to-JPG_QT.exe"
"@ | Set-Content -LiteralPath $cmdFile -Encoding ascii
# Start from an empty dist so files dropped from deployment (e.g. Windows SDK DLLs) do not linger.
$distPath=Join-Path $projectRoot $distName
if(Test-Path -LiteralPath $distPath){Remove-Item -LiteralPath $distPath -Recurse -Force}
& cmd.exe /d /c $cmdFile
if($LASTEXITCODE){throw "建置失敗：$LASTEXITCODE"}
# The MSVC runtime is not shipped; users install Microsoft's latest VC++ Redistributable (see README).
# 授權檔只用專案內的 LICENSE、THIRD_PARTY_NOTICES.md 與 licenses/（含 Qt 的 LGPLv3/GPLv3 全文）。
# 不再整包複製 Qt 安裝目錄的 Licenses，那裡混有 CMake 與文件授權，並非本程式散布的元件。
$licenseTarget=Join-Path $projectRoot "$distName\licenses"
if(Test-Path -LiteralPath $licenseTarget){Remove-Item -LiteralPath $licenseTarget -Recurse -Force}
Copy-Item -LiteralPath (Join-Path $projectRoot 'licenses') -Destination $licenseTarget -Recurse
foreach($document in 'LICENSE','THIRD_PARTY_NOTICES.md'){
    Copy-Item -LiteralPath (Join-Path $projectRoot $document) -Destination (Join-Path $projectRoot $distName) -Force
}
foreach($document in 'README.md','VERIFICATION.md'){
    if(Test-Path -LiteralPath (Join-Path $projectRoot $document)){
        Copy-Item -LiteralPath (Join-Path $projectRoot $document) -Destination (Join-Path $projectRoot $distName)
    }
}
