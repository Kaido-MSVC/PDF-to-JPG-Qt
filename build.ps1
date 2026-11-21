param([string]$QtRoot = 'D:\qt\6.12.0\msvc2022_64', [switch]$Testing)

$ErrorActionPreference = 'Stop'

function Publish-Package([string]$Stage, [string]$Destination, [string]$Backup) {
    $previous = Get-Item -LiteralPath $Destination -ErrorAction SilentlyContinue
    if ($previous) {
        if (!$previous.PSIsContainer -or ($previous.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "部署目的地不是一般資料夾：$Destination"
        }
        [IO.Directory]::Move($Destination, $Backup)
    }
    try {
        [IO.Directory]::Move($Stage, $Destination)
    } catch {
        if ($previous) {
            try { [IO.Directory]::Move($Backup, $Destination) }
            catch { throw "部署失敗且無法自動還原。上一份封裝仍保留於：$Backup。$($_.Exception.Message)" }
        }
        throw
    }
    if ($previous) { Write-Output "上一份封裝：$Backup" }
}

$projectRoot = $PSScriptRoot
$distName = if ($Testing) { 'dist-test' } else { 'dist' }
$testOption = if ($Testing) { 'ON' } else { 'OFF' }
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw '找不到 MSVC x64 工具鏈' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$cmake = (Get-Command cmake.exe -CommandType Application | Select-Object -First 1).Source
$ninja = 'D:\qt\Tools\Ninja\ninja.exe'
$deployer = Join-Path $QtRoot 'bin\windeployqt.exe'
$qtpaths = Join-Path $QtRoot 'bin\qtpaths.exe'
foreach ($path in $vcvars, $cmake, $ninja, $deployer, $qtpaths) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "找不到建置工具：$path" }
}
foreach ($relative in 'bin/Qt6Pdf.dll', 'plugins/imageformats/qpdf.dll') {
    $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $QtRoot $relative)
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=The Qt Company Oy(,|$)') {
        throw "Qt PDF 元件簽章無法驗證：$relative"
    }
}

$work = Join-Path $projectRoot 'work'
$run = Join-Path $work ("deploy-$distName-" + [Guid]::NewGuid().ToString('N').Substring(0, 12))
$build = Join-Path $run 'build'
$stage = Join-Path $run 'package'
$cmdFile = Join-Path $run 'build.cmd'
# 使用獨立建置快取與部署資料夾，正式版及測試版互不共用。
New-Item -ItemType Directory -Path $stage -Force | Out-Null
@'
@echo off
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem"
set "VSLANG=1033"
call "%PDF_BUILD_VCVARS%" -vcvars_ver=14.52
if errorlevel 1 exit /b 1
"%PDF_BUILD_CMAKE%" -S "%PDF_BUILD_SOURCE%" -B "%PDF_BUILD_BINARY%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPDF2JPG_TESTING=%PDF_BUILD_TESTING% -DQt6_DIR="%PDF_BUILD_QT%\lib\cmake\Qt6" -DQt6Pdf_DIR="%PDF_BUILD_QT%\lib\cmake\Qt6Pdf" -DCMAKE_PREFIX_PATH="%PDF_BUILD_QT%" -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF -DCMAKE_MAKE_PROGRAM="%PDF_BUILD_NINJA%"
if errorlevel 1 exit /b 1
"%PDF_BUILD_CMAKE%" --build "%PDF_BUILD_BINARY%" --parallel 4
if errorlevel 1 exit /b 1
copy /y "%PDF_BUILD_BINARY%\PDF-to-JPG_QT.exe" "%PDF_BUILD_STAGE%\PDF-to-JPG_QT.exe" >nul
if errorlevel 1 exit /b 1
set "PATH=%PDF_BUILD_QT%\bin;%SystemRoot%\System32;%SystemRoot%"
set "QT_PLUGIN_PATH="
set "QT_QPA_PLATFORM_PLUGIN_PATH="
"%PDF_BUILD_DEPLOYER%" --qtpaths "%PDF_BUILD_QTPATHS%" --release --no-compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler "%PDF_BUILD_STAGE%\PDF-to-JPG_QT.exe"
exit /b %errorlevel%
'@ | Set-Content -LiteralPath $cmdFile -Encoding ascii

$command = [Diagnostics.ProcessStartInfo]::new($env:ComSpec, "/d /v:off /s /c `"`"$cmdFile`"`"")
$command.UseShellExecute = $false
$command.WorkingDirectory = $run
$variables = @{
    VCVARS = $vcvars; CMAKE = $cmake; NINJA = $ninja; SOURCE = $projectRoot; BINARY = $build
    TESTING = $testOption; QT = $QtRoot; STAGE = $stage; DEPLOYER = $deployer; QTPATHS = $qtpaths
}
foreach ($key in $variables.Keys) { $command.EnvironmentVariables["PDF_BUILD_$key"] = $variables[$key] }
foreach ($key in @('CMAKE_PREFIX_PATH', 'Qt6_DIR', 'Qt6_ROOT', 'Qt6Pdf_DIR', 'Qt6Pdf_ROOT', 'QTDIR')) {
    $command.EnvironmentVariables.Remove($key)
}
$process = [Diagnostics.Process]::Start($command)
try {
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "建置或部署失敗：$($process.ExitCode)。工作目錄：$run" }
} finally {
    $process.Dispose()
}

$qtPrefix = $QtRoot.TrimEnd('\') + '\'
foreach ($entry in (Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') -Pattern '^Qt6\w*_DIR:[^=]+=(.+)$')) {
    $packagePath = [IO.Path]::GetFullPath($entry.Matches[0].Groups[1].Value)
    if (!$packagePath.StartsWith($qtPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "CMake 引用了其他 Qt SDK：$packagePath"
    }
}

# 維持原有部署規則，附授權文字和使用說明，省略開發文件與 Microsoft 執行階段。
$guide = Join-Path $projectRoot '使用說明.txt'
$version = [regex]::Match((Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw),
                         'project\([^)]*VERSION\s+([\d.]+)').Groups[1].Value
$stagedExe = Get-Item -LiteralPath (Join-Path $stage 'PDF-to-JPG_QT.exe')
if (!$version -or $stagedExe.VersionInfo.FileVersion -ne $version -or
    !(Get-Content -LiteralPath $guide -Raw -Encoding utf8).Contains("版本 $version")) {
    throw 'CMake、執行檔與使用說明版本不一致。'
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'licenses') -Destination $stage -Recurse
foreach ($name in 'LICENSE', 'THIRD_PARTY_NOTICES.md', '使用說明.txt') {
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $stage
}
foreach ($relative in 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Pdf.dll',
          'platforms/qwindows.dll', 'imageformats/qjpeg.dll', 'imageformats/qpdf.dll',
          'licenses/LGPL-3.0.txt', 'licenses/GPL-3.0.txt', 'licenses/Qt-LICENSE.txt') {
    if (!(Test-Path -LiteralPath (Join-Path $stage $relative) -PathType Leaf)) { throw "封裝缺少：$relative" }
}
foreach ($file in Get-ChildItem -LiteralPath $stage -File -Recurse -Filter '*.dll') {
    $relative = $file.FullName.Substring($stage.Length + 1)
    $sdkDirectory = if ($file.DirectoryName -eq $stage) { 'bin' } else { 'plugins' }
    $sdkFile = Join-Path (Join-Path $QtRoot $sdkDirectory) $relative
    if (!(Test-Path -LiteralPath $sdkFile -PathType Leaf) -or
        (Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $sdkFile).Hash) {
        throw "封裝元件與選定的 Qt SDK 不符：$relative"
    }
}

# 在同一磁碟區重新命名。保留上一份封裝，以便部署失敗時還原。
$dist = Join-Path $projectRoot $distName
$backup = Join-Path $run 'previous-package'
$lock = [IO.File]::Open((Join-Path $work "deploy-$distName.lock"), 'OpenOrCreate', 'ReadWrite', 'None')
try {
    Publish-Package $stage $dist $backup
} finally {
    $lock.Dispose()
}
Write-Output "建置目錄：$build"
Write-Output "封裝目錄：$dist"
