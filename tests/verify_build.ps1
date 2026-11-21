param([string]$QtRoot = 'D:\qt\6.12.0\msvc2022_64')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $root ('work/build-verification-' + (Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
foreach ($name in 'CMakeLists.txt', 'main.cpp', 'converter.cpp', 'converter.h', 'resume.h',
         'build.ps1', 'app.rc', 'app.manifest', 'resources.qrc', 'assets', 'tests',
         'LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses', '使用說明.txt') {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $fixture -Recurse
}

$checks = [Collections.Generic.List[string]]::new()
$shell = (Get-Process -Id $PID).Path
$buildScript = Join-Path $fixture 'build.ps1'
foreach ($name in 'dist', 'dist-test') {
    $folder = Join-Path $fixture $name
    New-Item -ItemType Directory -Path $folder | Out-Null
    Set-Content -LiteralPath (Join-Path $folder 'previous.txt') -Value "previous $name" -Encoding ascii
}

function Assert-PreviousPackages {
    foreach ($name in 'dist', 'dist-test') {
        $files = @(Get-ChildItem -LiteralPath (Join-Path $fixture $name) -Force)
        if ($files.Count -ne 1 -or $files[0].Name -ne 'previous.txt' -or
            (Get-Content -LiteralPath $files[0].FullName) -ne "previous $name") {
            throw "既有測試封裝被修改：$name"
        }
    }
}

function Assert-BuildFails([string]$Name, [string]$Expected, [string[]]$Arguments) {
    $log = Join-Path $fixture "$Name.log"
    & $shell -NoProfile -File $buildScript -QtRoot $QtRoot @Arguments *> $log
    if ($LASTEXITCODE -eq 0 -or !(Select-String -LiteralPath $log -SimpleMatch $Expected -Quiet)) {
        throw "建置未在預期階段失敗：$log"
    }
    Assert-PreviousPackages
    $checks.Add($Name)
}

Add-Content -LiteralPath (Join-Path $fixture 'converter.cpp') -Value "`n#error PDF2JPG_TEST_COMPILER_FAILURE"
Assert-BuildFails 'compiler-failure' 'PDF2JPG_TEST_COMPILER_FAILURE'
Copy-Item -LiteralPath (Join-Path $root 'converter.cpp') -Destination $fixture -Force

Set-Content -LiteralPath (Join-Path $fixture 'invalid-payload.txt') -Value 'test-only non-PE'
Add-Content -LiteralPath (Join-Path $fixture 'CMakeLists.txt') -Value @'

add_custom_command(TARGET PDF-to-JPG_QT POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/invalid-payload.txt" "$<TARGET_FILE:PDF-to-JPG_QT>")
'@
Assert-BuildFails 'deployment-failure-testing' 'DOS header check failed' @('-Testing')

# 直接呼叫腳本中的發布函式，在舊目錄已改名後模擬候選封裝移動失敗。
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($buildScript, [ref]$null, [ref]$parseErrors)
if ($parseErrors) { throw $parseErrors[0] }
$definition = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Publish-Package'
}, $false)
. ([scriptblock]::Create($definition.Extent.Text))
foreach ($name in 'dist', 'dist-test') {
    $backup = Join-Path $fixture "$name-backup"
    $failed = $false
    try { Publish-Package (Join-Path $fixture 'absent-package') (Join-Path $fixture $name) $backup }
    catch [IO.IOException] { $failed = $true }
    if (!$failed -or (Test-Path -LiteralPath $backup)) { throw "未能還原 $name" }
    Assert-PreviousPackages
    $checks.Add("rollback-$name")
}

[pscustomobject]@{ passed = $true; checks = $checks } | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $fixture 'summary.json') -Encoding utf8
Write-Output "PASS $($checks.Count)：$fixture"
