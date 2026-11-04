param(
    [string]$Compiler = "C:\Program Files\Windhawk\Compiler\bin\clang++.exe",
    [string]$Suite = "all"
)

$ErrorActionPreference = "Stop"
$WorktreeDir = Split-Path -Parent $PSScriptRoot
Set-Location $WorktreeDir

function Run-TestDll([string]$dllName, [string]$sourceTest, [string[]]$sourceEngine) {
    Write-Host "Compiling test runner for $sourceTest..."
    $allSources = @($sourceTest) + $sourceEngine
    & $Compiler -target x86_64-w64-windows-gnu -std=c++23 -static -I src $allSources -shared -o $dllName
    if ($LASTEXITCODE -ne 0) {
        Write-Error "DLL Compilation failed for $dllName"
        exit $LASTEXITCODE
    }

    $dllPath = (Join-Path $WorktreeDir $dllName).Replace('\', '\\')

    $script = @"
`$sig = @'
[DllImport(@"$dllPath", EntryPoint = "RunTestsDll")]
public static extern int Run();
'@
`$type = Add-Type -MemberDefinition `$sig -Name "TestRunner_$([System.Guid]::NewGuid().ToString('N'))" -Namespace "CalendarTest" -PassThru
`$code = `$type::Run()
exit `$code
"@

    $bytes = [System.Text.Encoding]::Unicode.GetBytes($script)
    $encoded = [Convert]::ToBase64String($bytes)
    & powershell.exe -NoProfile -EncodedCommand $encoded
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Tests failed in $dllName with exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}

$engineSources = @("src/calendar_engine/ics_parser.cpp", "src/calendar_engine/teams_extractor.cpp")
if (Test-Path "src/calendar_engine/meeting_status.cpp") {
    $engineSources += "src/calendar_engine/meeting_status.cpp"
}

if ($Suite -eq "all" -or $Suite -eq "ics") {
    Run-TestDll "build_test_ics.dll" "tests/test_ics_parser.cpp" $engineSources
}

if ($Suite -eq "all" -or $Suite -eq "teams") {
    Run-TestDll "build_test_teams.dll" "tests/test_teams_extractor.cpp" $engineSources
}

Write-Host "All requested test suites passed!"
exit 0
