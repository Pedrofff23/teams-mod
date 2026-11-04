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

$modSources = @()
if (Test-Path "src/mod/mod_settings.cpp") {
    $modSources += "src/mod/mod_settings.cpp"
}
if (Test-Path "src/mod/calendar_cache.cpp") {
    $modSources += "src/mod/calendar_cache.cpp"
}
if (Test-Path "src/mod/http_fetcher.cpp") {
    $modSources += "src/mod/http_fetcher.cpp"
}
if (Test-Path "src/mod/teams_calendar_flyout.wh.cpp") {
    $modSources += "src/mod/teams_calendar_flyout.wh.cpp"
}

$uiSources = @()
if (Test-Path "src/ui/agenda_view_model.cpp") {
    $uiSources += "src/ui/agenda_view_model.cpp"
}
if (Test-Path "src/ui/xaml_injector.cpp") {
    $uiSources += "src/ui/xaml_injector.cpp"
}

if ($Suite -eq "all" -or $Suite -eq "ics") {
    Run-TestDll "build_test_ics.dll" "tests/test_ics_parser.cpp" $engineSources
}

if ($Suite -eq "all" -or $Suite -eq "teams") {
    Run-TestDll "build_test_teams.dll" "tests/test_teams_extractor.cpp" $engineSources
}

if ($Suite -eq "all" -or $Suite -eq "mod") {
    Run-TestDll "build_test_mod.dll" "tests/test_mod_skeleton_http.cpp" ($engineSources + $modSources + $uiSources + @("-lwininet", "-lole32", "-lshell32"))
}

if ($Suite -eq "all" -or $Suite -eq "ui") {
    Run-TestDll "build_test_ui.dll" "tests/test_agenda_ui.cpp" ($engineSources + $modSources + $uiSources + @("-lwininet", "-lole32", "-lshell32"))
}

if ($Suite -eq "all" -or $Suite -eq "join" -or $Suite -eq "guide") {
    Run-TestDll "build_test_join.dll" "tests/test_one_click_join_guide.cpp" ($engineSources + $modSources + $uiSources + @("-lwininet", "-lole32", "-lshell32"))
}

Write-Host "All requested test suites passed!"
exit 0
