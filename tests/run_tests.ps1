param(
    [string]$Compiler = "C:\Program Files\Windhawk\Compiler\bin\clang++.exe"
)

$ErrorActionPreference = "Stop"
$WorktreeDir = Split-Path -Parent $PSScriptRoot
Set-Location $WorktreeDir

Write-Host "Compiling test runner..."
& $Compiler -target x86_64-w64-windows-gnu -std=c++23 -static -I src tests/test_ics_parser.cpp src/calendar_engine/ics_parser.cpp -shared -o build_test_ics.dll
if ($LASTEXITCODE -ne 0) {
    Write-Error "DLL Compilation failed"
    exit $LASTEXITCODE
}

& $Compiler -target x86_64-w64-windows-gnu -std=c++23 -static -I src tests/test_ics_parser.cpp src/calendar_engine/ics_parser.cpp -o build_test_ics.exe
if ($LASTEXITCODE -ne 0) {
    Write-Error "EXE Compilation failed"
    exit $LASTEXITCODE
}

$dllPath = (Join-Path $WorktreeDir "build_test_ics.dll").Replace('\', '\\')

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
exit $LASTEXITCODE
