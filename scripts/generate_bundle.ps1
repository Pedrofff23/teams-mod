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
// @github          https://github.com/Pedrofff23/teams-mod
// @include         ShellHost.exe
// @include         ShellExperienceHost.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lwininet -lshell32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Microsoft Teams Calendar Flyout

Integrates Microsoft Teams upcoming meetings directly into the native Windows 11 taskbar calendar flyout (Notification Center / Clock flyout).

## Features
- **Daily Agenda**: Displays today's schedule directly inside the calendar flyout when clicking the taskbar clock.
- **Dynamic Meeting Status**: Color-coded badges indicating meeting status:
  - **Live** (Green): Meeting is currently in progress.
  - **Soon** (Amber): Meeting starts within 15 minutes.
  - **Future**: Upcoming meeting later today.
  - **Past**: Completed meeting earlier today.
- **One-Click Join**: Quick "Join Meeting" button launching directly into Microsoft Teams.
- **Background Sync**: Periodically fetches and parses your Outlook / Teams calendar feed via WinINet.
- **Native Fluent UI**: Blends seamlessly with Windows 11 Dark and Light themes.
- **Privacy-first**: Connects directly to your calendar feed URL without intermediate proxy servers.

## How to Configure
1. Open the **Settings** tab of this mod in Windhawk.
2. In **Outlook / Teams ICS Feed URL**, paste your published calendar ICS link:
   - Go to [Outlook on the web](https://outlook.office.com/mail/) -> **Settings** (gear icon) -> **Calendar** -> **Shared calendars**.
   - Under **Publish a calendar**, select your calendar and set permissions to *Can view all details*, then click **Publish**.
   - Copy the **ICS** link and paste it into the mod settings.
3. Configure your desired **Refresh Interval** (default: 15 minutes).
4. Choose whether to show past meetings or enable the join button.
5. Click **Save** in Windhawk. Open your Windows 11 clock flyout to view your agenda!
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- feed_url: ""
  $name: Outlook / Teams ICS Feed URL
  $description: Published ICS calendar URL from Outlook Web or Microsoft Teams
- refresh_interval_minutes: 15
  $name: Refresh Interval (minutes)
  $description: How often to check for calendar updates (minimum 1)
- show_past_events: false
  $name: Show Past Events
  $description: Show meetings that have already ended earlier today
- enable_join_button: true
  $name: Enable One-Click Join Button
  $description: Display direct Join Meeting button for Microsoft Teams links
*/
// ==/WindhawkModSettings==

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <wininet.h>
#include <shellapi.h>
#undef GetCurrentTime
#ifdef WH_MOD
#include <windhawk_api.h>
#ifndef WH_MOD_ID
#define WH_MOD_ID L"teams-calendar-flyout"
#endif
#include <windhawk_utils.h>

#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.System.UserProfile.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/base.h>
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
#include <iomanip>

'@

# Collect headers and implementation content from src/ in topological order
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
    $inWindhawkMetadata = $false
    foreach ($line in $lines) {
        $trimmed = $line.Trim()

        # Handle Windhawk metadata blocks (skip completely when combining source files)
        if ($trimmed -like '// ==WindhawkMod*' -and $trimmed -notlike '// ==/WindhawkMod*') {
            $inWindhawkMetadata = $true
            continue
        }
        if ($inWindhawkMetadata) {
            if ($trimmed -like '// ==/WindhawkMod*') {
                $inWindhawkMetadata = $false
            }
            continue
        }
        if ($trimmed -like '// ==/WindhawkMod*') {
            continue
        }

        # Skip pragma once and redundant system/winrt includes
        if ($trimmed -eq "#pragma once" -or $trimmed -eq "#undef GetCurrentTime") { continue }
        if ($trimmed -like '#include "*"' -or $trimmed -like '#include <windhawk_api.h>' -or $trimmed -like '#include <windhawk_utils.h>' -or $trimmed -like '#include <winrt/*>') { continue }

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

# Write to canonical root teams-calendar-flyout.wh.cpp
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText((Join-Path $WorktreeDir "teams-calendar-flyout.wh.cpp"), $outputContent, $utf8NoBom)
Write-Host "Wrote root teams-calendar-flyout.wh.cpp ($($outputContent.Length) bytes)"

Write-Host "Bundle generation complete!"
