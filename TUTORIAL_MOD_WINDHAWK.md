# Microsoft Teams Calendar Flyout for Windows 11: Developer & User Guide

A step-by-step guide to installing, configuring, compiling, and debugging the Microsoft Teams Calendar Flyout mod for Windows 11 using [Windhawk](https://windhawk.net/).

---

## Table of Contents
1. [Overview & Architecture](#1-overview--architecture)
2. [Prerequisites](#2-prerequisites)
3. [Obtaining Your Secret Outlook / Teams ICS Calendar URL](#3-obtaining-your-secret-outlook--teams-ics-calendar-url)
4. [Installing & Compiling the Mod in Windhawk](#4-installing--compiling-the-mod-in-windhawk)
5. [Configuring Mod Settings](#5-configuring-mod-settings)
6. [One-Click Join Action & Visual Design](#6-one-click-join-action--visual-design)
7. [Inspecting Runtime Logs (Wh_Log)](#7-inspecting-runtime-logs-wh_log)
8. [Troubleshooting & FAQ](#8-troubleshooting--faq)

---

## 1. Overview & Architecture

### What is this mod?
In Windows 10, clicking the notification center clock opened a flyout displaying both a monthly calendar and the user's upcoming agenda. In Windows 11, Microsoft removed the agenda view, leaving only a static date picker grid.

The **Microsoft Teams Calendar Flyout** Windhawk mod restores this capability with first-class Microsoft Teams integration. It runs seamlessly inside the Windows 11 system shell, periodically syncing your calendar feed in the background and dynamically injecting an interactive, Fluent Design agenda panel right into the calendar flyout.

### How it integrates into the Windows 11 Shell
- **Target Process:** Windows 11 hosts its taskbar notification and calendar flyouts inside `ShellHost.exe` (and on some Windows builds, `ShellExperienceHost.exe`).
- **Target Container (`CalendarCenterGrid`):** The Windows 11 calendar flyout is built with WinUI / XAML. Its primary layout container is named `CalendarCenterGrid`.
- **Seam & Injection:** The mod detects the activation of `CalendarCenterGrid`, dynamically generates WinUI XAML markup representing meetings for the user-selected date, and mounts a `ScrollViewer` (`TeamsAgendaScrollViewer`) directly below the calendar grid.
- **Reactivity:** When you click different dates on the calendar grid or when the system theme switches between Light and Dark mode, the agenda updates instantaneously without flickering.
- **Background Sync Engine:** A dedicated background worker thread polls your published ICS feed at a configurable cadence (defaulting to 15 minutes) using native WinINet API calls, updating a thread-safe, in-memory cache with offline fallback resilience.

---

## 2. Prerequisites

1. **Windows 11:** Compatible with Windows 11 21H2, 22H2, 23H2, 24H2, and later.
2. **Windhawk:** Windhawk v1.4 or newer installed from [windhawk.net](https://windhawk.net/).
3. **Microsoft 365 or Outlook Account:** A personal or work/school account that supports published iCalendar (ICS) feeds.
4. **Microsoft Teams:** Microsoft Teams desktop client or web browser (Chrome, Edge) to launch joined meetings.

---

## 3. Obtaining Your Secret Outlook / Teams ICS Calendar URL

The mod syncs your schedule via Microsoft 365's RFC 5545 published iCalendar link. This requires **no tenant admin consent**, **no Azure App Registration**, and **no Graph API tokens**.

Follow these exact menu paths to obtain your private ICS link:

### Step-by-Step Instructions:

1. **Open Outlook on the Web:**
   Navigate to [outlook.office.com](https://outlook.office.com) (for work or school accounts) or [outlook.live.com](https://outlook.live.com) (for personal Microsoft accounts). Sign in with your credentials.

2. **Open Settings:**
   Click the gear icon (**Settings**) in the top-right corner of the Outlook navigation bar.

3. **Navigate to Shared Calendars:**
   In the Settings sidebar, select:
   - **Calendar** &rarr; **Shared calendars**

4. **Locate "Publish a calendar":**
   Scroll down to the section titled **Publish a calendar**.

5. **Select Your Calendar & Permissions:**
   - Under **Select a calendar**, choose **Calendar** (or your primary work calendar).
   - Under **Select permissions**, choose:
     - **Can view all details** (Recommended: ensures meeting titles, descriptions, and Teams join links are visible in the flyout).
     - *Note:* Choosing "Can view when I'm busy" only exposes free/busy slots and will omit meeting titles and Teams links.

6. **Generate and Copy the ICS Link:**
   - Click the **Publish** button.
   - Two links will appear: an **HTML link** and an **ICS link**.
   - Click on the **ICS link** and select **Copy link**.
   - The copied URL will follow this structure:
     ```text
     https://outlook.office365.com/owa/calendar/00000000-0000-0000-0000-000000000000@yourdomain.com/00000000-0000-0000-0000-000000000000/reachcalendar.ics
     ```
     *(Or on personal Outlook: `https://outlook.live.com/owa/calendar/.../reachcalendar.ics`)*.

> [!IMPORTANT]
> **Keep your ICS link private:** Anyone with this link can view the details of events published on that calendar. Do not post it in public repositories or share it with unauthorized individuals.

---

## 4. Installing & Compiling the Mod in Windhawk

Windhawk allows you to compile and load community mods directly on your machine.

### Step-by-Step Compilation Workflow:

1. **Launch Windhawk:**
   Open the **Windhawk** application from your Start Menu.

2. **Enable Developer Mode (if not already enabled):**
   - Click on the three dots menu (**&vellip;**) in the top right corner of Windhawk and select **Settings**.
   - In the **Advanced** section, toggle on **Developer Mode**.
   - Return to the main Windhawk window.

3. **Create a New Mod:**
   - On the top navigation bar, click **Developer Channel** or the **+ New Mod** button.
   - Set the Mod ID to `teams-calendar-flyout`.
   - Click **Create**.

4. **Paste the Mod Source Code:**
   - Open the mod source file from this repository:
     [`teams-calendar-flyout.wh.cpp`](teams-calendar-flyout.wh.cpp) (or [`dist/teams-calendar-flyout.wh.cpp`](dist/teams-calendar-flyout.wh.cpp)).
   - Select all (`Ctrl+A`) and copy (`Ctrl+C`).
   - In the Windhawk code editor tab, replace the default template completely with the copied code.

5. **Compile the Mod:**
   - Click the **Compile** button in the Windhawk editor toolbar.
   - Windhawk invokes its bundled Clang C++23 compiler (`clang++.exe`). The compilation takes ~3-5 seconds.
   - The status bar at the bottom should display: `Compilation successful`.

6. **Configure the Feed URL:**
   - Switch to the **Settings** tab in the Windhawk mod page.
   - In the **Outlook / Teams ICS Feed URL** field, paste your copied ICS link.
   - Click **Accept** or **Save Settings**.

7. **Verify Injection:**
   - Click the Clock / Date display in your Windows 11 Taskbar to open the calendar flyout.
   - You will see the **Teams Agenda** panel seamlessly rendered below the calendar dates!

---

## 5. Configuring Mod Settings

You can customize the mod's behavior at any time via Windhawk's **Settings** tab without recompiling:

| Setting Key | Display Name | Type | Default | Description |
|:---|:---|:---:|:---:|:---|
| `feed_url` | **Outlook / Teams ICS Feed URL** | `string` | `""` | The secret HTTPS ICS link copied from Outlook on the Web. |
| `refresh_interval_minutes` | **Refresh Interval (minutes)** | `int` | `15` | Polling frequency for fetching updates. Minimum is 1 minute. |
| `show_past_events` | **Show Past Events** | `bool` | `false` | When unchecked, meetings that have already ended earlier today are hidden. When checked, past events remain visible with a gray "Past" badge. |
| `enable_join_button` | **Enable One-Click Join Button** | `bool` | `true` | When checked, meetings containing a Microsoft Teams link render an interactive Fluent "Join Meeting" button. |

Whenever you update a setting and click **Accept**, the mod immediately reloads its configuration via `Wh_ModSettingsChanged`, updates the background sync timer, and refreshes the XAML UI in real time.

---

## 6. One-Click Join Action & Visual Design

### Fluent Design Alignment
The injected agenda panel strictly follows Windows 11 Fluent Design guidelines:
- **Rounded Corners:** Meeting cards feature `CornerRadius="6"`, and action buttons feature `CornerRadius="4"`.
- **System Theme Harmony:** The mod queries the Windows registry (`AppsUseLightTheme`) and high-contrast system flags (`SPI_GETHIGHCONTRAST`) to match the shell's active palette:
  - **Dark Mode:** Cards use `#202020` on `#383838` borders with Teams accent `#6264A7`.
  - **Light Mode:** Cards use `#FFFFFF` on `#E5E5E5` borders with Teams accent `#464EB8`.
  - **High Contrast:** Uses pure `#000000` with high-visibility `#00FFFF` and `#FFFF00` accents.
- **Dynamic Status Badges:**
  - 🟢 **Live Now:** Meetings currently in progress (`#107C41`).
  - 🟠 **Starting Soon:** Meetings beginning within 15 minutes (`#D83B01`).
  - 🔵 **Upcoming:** Meetings scheduled for later today (`#0078D4`).
  - ⚪ **Past:** Finished meetings (`#797775`).

### One-Click Join Behavior & Security Dispatch
- **Direct System Dispatch:** Clicking the **Join Meeting** button reads the sanitized URL from the XAML element and invokes `winrt::Windows::System::Launcher::LaunchUriAsync` with `LauncherOptions.FallbackUri` to launch the native Microsoft Teams desktop client (`msteams:` protocol) with web fallback, falling back to `ShellExecuteW`.
- **Desktop Client & Browser Routing:** If the Microsoft Teams desktop client is registered for `https://teams.microsoft.com` or `msteams:` protocols, Windows launches Teams directly. Otherwise, it opens in your default web browser.
- **Protocol Sanitization:** To guarantee system security and prevent command injection, the URL dispatcher strictly allows only `https://`, `http://`, and `msteams:` protocols. Any attempt to pass executable binaries, shell scripts, or arbitrary URI schemes (e.g. `file:`, `cmd:`, `powershell:`, `javascript:`) is detected and blocked before dispatching.
- **SafeLinks Protection:** The extractor automatically unwraps Microsoft Defender SafeLinks (`*.safelinks.protection.outlook.com/?url=...`), routing directly to the target Teams meeting room without redirect delays.

---

## 7. Inspecting Runtime Logs (Wh_Log)

When developing or troubleshooting the mod, you can inspect live diagnostics using Windhawk's built-in logging facility.

### How to View Logs:
1. In Windhawk, navigate to the **Microsoft Teams Calendar Flyout** mod page.
2. Click the **Mod Log** tab.
3. Windhawk displays log messages emitted by `Wh_LogConsole` and `Wh_Log`.

### Log Message Prefixes:
- `[TeamsCalendar] Initializing mod lifecycle...`: Mod startup and settings load.
- `[TeamsCalendar] Injecting agenda container into CalendarCenterGrid`: Successful UI mounting into the Windows 11 shell.
- `[TeamsCalendar] Background sync triggered for feed: https://...`: WinINet HTTP fetch initiated.
- `[TeamsCalendar] Calendar feed parsed successfully: 14 events loaded`: Events parsed from the RFC 5545 stream.
- `[TeamsCalendar] Joining meeting: https://teams.microsoft.com/...`: User clicked a Join Meeting action.
- `[TeamsCalendar] Warning: Failed to fetch feed (HTTP 404/500/offline)`: Network or server error; cache retained offline.

---

## 8. Troubleshooting & FAQ

### 1. Smart App Control / Antivirus Warnings
**Symptom:** Windows displays a Smart App Control warning when compiling or running test executables.  
**Resolution:**
- Compiling the mod **inside Windhawk GUI** executes through Windhawk's trusted engine and is fully compatible with Smart App Control.
- For local repository test suites, our build script (`tests/run_tests.ps1`) compiles test suites to dynamic link libraries (`.dll`) and loads them in-process through PowerShell, conforming strictly with Smart App Control policies without requiring system-wide policy disabling.

### 2. The Agenda Panel Does Not Appear in the Flyout
**Checklist:**
1. Verify that `ShellHost.exe` is running in Task Manager.
2. Confirm the mod is enabled in Windhawk (the toggle switch on the mod page must be ON).
3. Ensure you have entered your ICS URL in the **Settings** tab and clicked **Accept**.
4. Check the **Mod Log** tab in Windhawk to verify that `CalendarCenterGrid` was located and injected.
5. If Windows 11 recently updated, restart `explorer.exe` (via Task Manager &rarr; Windows Explorer &rarr; Restart) so `ShellHost.exe` reloads the mod.

### 3. Meetings Do Not Show Up / "No meetings scheduled for this day"
- **Date Check:** Click the specific date on the calendar grid that contains meetings. By default, the agenda displays the selected day.
- **Publish Permissions:** Ensure you selected **"Can view all details"** when publishing your calendar in Outlook Web. If set to "Can view when I'm busy", meeting details are omitted from the ICS feed.
- **Sync Cadence:** Wait up to your configured `refresh_interval_minutes` (default 15 minutes) or click Save in the mod settings to force an immediate refresh.

### 4. Offline Resilience
If your device loses Wi-Fi or Internet access, the mod retains the last successfully fetched calendar state in its in-memory cache. You can continue navigating dates and viewing your schedule offline. Once connectivity returns, the background worker silently resynchronizes with Microsoft 365.
