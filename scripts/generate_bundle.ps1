# scripts/generate_bundle.ps1
$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$WorktreeDir = Split-Path -Parent $ScriptDir
Set-Location $WorktreeDir

Write-Host "Generating self-contained Windhawk Mod bundle..."

$bundleHeader = @'
// ==WindhawkMod==
// @id              teams-calendar-flyout
// @name            Microsoft Teams Calendar Flyout
// @description     Displays upcoming Microsoft Teams meetings in the Windows 11 calendar flyout
// @version         1.0.0
// @author          Pedrofff23
// @include         ShellHost.exe
// @include         ShellExperienceHost.exe
// @compilerOptions -lwininet -lole32 -lshell32
// ==/WindhawkMod==

// ==WindhawkModSettings==
// - feed_url: ""
//   $name: Outlook / Teams ICS Feed URL
//   $description: Published ICS calendar URL from Outlook Web or Microsoft Teams
// - refresh_interval_minutes: 15
//   $name: Refresh Interval (minutes)
//   $description: How often to check for calendar updates (minimum 1)
// - show_past_events: false
//   $name: Show Past Events
//   $description: Show meetings that have already ended earlier today
// - enable_join_button: true
//   $name: Enable One-Click Join Button
//   $description: Display direct Join Meeting button for Microsoft Teams links
// ==/WindhawkModSettings==

#include <windows.h>
#include <wininet.h>
#include <shellapi.h>
#ifdef WH_MOD
#include <windhawk_api.h>
#endif
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <memory>
#include <sstream>
#include <cstdio>
#include <ctime>
#include <cstdint>
#include <cctype>
#include <optional>
#include <functional>
#include <compare>
#include <array>

'@

# Collect headers and implementation content from src/ in topological order
# We will combine the headers and cpp files cleanly into a single compilation unit.
$headers = @(
    "src/calendar_engine/meeting_status.h",
    "src/calendar_engine/ics_parser.h",
    "src/calendar_engine/teams_extractor.h",
    "src/mod/mod_settings.h",
    "src/mod/calendar_cache.h",
    "src/mod/http_fetcher.h",
    "src/ui/agenda_view_model.h",
    "src/ui/xaml_injector.h",
    "src/mod/teams_calendar_flyout.h"
)

$sources = @(
    "src/calendar_engine/meeting_status.cpp",
    "src/calendar_engine/ics_parser.cpp",
    "src/calendar_engine/teams_extractor.cpp",
    "src/mod/mod_settings.cpp",
    "src/mod/calendar_cache.cpp",
    "src/mod/http_fetcher.cpp",
    "src/ui/agenda_view_model.cpp",
    "src/ui/xaml_injector.cpp",
    "src/mod/teams_calendar_flyout.wh.cpp"
)

function Clean-Content([string]$fileContent) {
    $lines = $fileContent -split "`r?`n"
    $cleanLines = @()
    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        # Skip pragma once, local includes, Windhawk metadata if inside src/
        if ($trimmed -eq "#pragma once") { continue }
        if ($trimmed -like '#include "*"' -or $trimmed -like '#include <windhawk_api.h>') { continue }
        if ($trimmed -like '// ==WindhawkMod*' -or $trimmed -like '// @*' -or $trimmed -like '// - *' -or $trimmed -like '//   $*') { continue }
        $cleanLines += $line
    }
    return ($cleanLines -join "`n")
}

$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine($bundleHeader)

Write-Host "Appending declarations..."
foreach ($h in $headers) {
    if (Test-Path $h) {
        $raw = Get-Content -Raw -Path $h
        $cleaned = Clean-Content $raw
        [void]$sb.AppendLine("// --- Header: $h ---")
        [void]$sb.AppendLine($cleaned)
        [void]$sb.AppendLine()
    }
}

Write-Host "Appending implementations..."
foreach ($s in $sources) {
    if (Test-Path $s) {
        $raw = Get-Content -Raw -Path $s
        $cleaned = Clean-Content $raw
        [void]$sb.AppendLine("// --- Source: $s ---")
        [void]$sb.AppendLine($cleaned)
        [void]$sb.AppendLine()
    }
}

$outputContent = $sb.ToString()

# Write to root teams-calendar-flyout.wh.cpp
Set-Content -Path "teams-calendar-flyout.wh.cpp" -Value $outputContent -Encoding utf8
Write-Host "Wrote root teams-calendar-flyout.wh.cpp ($($outputContent.Length) bytes)"

# Ensure dist exists and copy to dist/teams-calendar-flyout.wh.cpp
if (-not (Test-Path "dist")) {
    New-Item -ItemType Directory -Path "dist" | Out-Null
}
Set-Content -Path "dist/teams-calendar-flyout.wh.cpp" -Value $outputContent -Encoding utf8
Write-Host "Wrote dist/teams-calendar-flyout.wh.cpp"

Write-Host "Bundle generation complete!"
