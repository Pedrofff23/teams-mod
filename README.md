# Microsoft Teams Calendar Flyout for Windows 11 (Windhawk Mod)

[English](README.md) | [Português](README.pt-BR.md)

[![Windhawk Mod](https://img.shields.io/badge/Windhawk-Mod-blue.svg)](https://windhawk.net/)
[![Windows 11](https://img.shields.io/badge/Windows-11%20WinUI-0078D4.svg)](https://www.microsoft.com/windows)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/)
[![Tests](https://img.shields.io/badge/Tests-Passing-brightgreen.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A lightweight [Windhawk](https://windhawk.net/) mod that restores upcoming **Microsoft Teams** daily agendas directly inside the native **Windows 11** taskbar calendar and notification flyout (`ShellHost.exe` / `ShellExperienceHost.exe`).

---

## 🎯 The Problem

In Windows 11, Microsoft redesigned the taskbar notification center and clock flyout, removing the integrated daily agenda view that previously existed in Windows 10. For professionals and teams relying on Microsoft Teams throughout the day, checking upcoming meeting times or joining calls requires switching focus, opening the heavy Teams desktop client, or navigating through web portals.

## 🚀 The Solution

This mod injects a modern, responsive, Fluent Design agenda directly below the Windows 11 calendar month grid:
- 📅 **Background Sync**: Periodically fetches and parses events from your official Outlook / Microsoft Teams iCalendar (`.ics`) feed without freezing the shell or taskbar.
- 🔗 **Automatic Teams Detection**: Automatically identifies Teams meetings (commercial, GCC/government, personal, or wrapped inside Outlook SafeLinks).
- ⚡ **One-Click Join**: Interactive Fluent Design "Join Meeting" button launching directly into the native Microsoft Teams app (`msteams:` protocol) with seamless browser fallback.
- 🟢 **Real-Time Meeting Status Indicators**:
  - `Live Now`: Meeting currently in progress.
  - `Starting Soon`: Meeting starting within the next 15 minutes.
  - `Upcoming`: Subsequent meetings scheduled for later today.
  - `Past`: Completed meetings earlier today (optionally hidden).
- 🔁 **Recurrence Expansion (RRULE)**: Full RFC 5545 recurrence engine expanding daily, weekly (e.g. Mon/Wed/Fri standups), and interval recurring events.
- 🌓 **Fluent Theme Adaptability**: Automatically harmonizes with Windows 11 Light and Dark themes, respecting high-contrast modes and accessible palettes.
- 📆 **Interactive Date Navigation**: Click any date on the calendar grid to instantly view the agenda for that day.
- 💾 **Offline Resilience**: Thread-safe in-memory cache; your schedule remains readable even when disconnected from Wi-Fi or the internet.

---

## 📦 Project Structure

```text
├── teams-calendar-flyout.wh.cpp     # Self-contained mod bundle ready for Windhawk
├── TUTORIAL_MOD_WINDHAWK.md        # Comprehensive step-by-step setup and developer guide
├── README.md                       # Main English documentation
├── README.pt-BR.md                 # Portuguese documentation
├── LICENSE                         # MIT License
├── src/
│   ├── calendar_engine/            # RFC 5545 calendar processing core
│   │   ├── ics_parser.h / .cpp     # iCalendar parser, unfolding, and RRULE recurrence expansion
│   │   ├── teams_extractor.h / .cpp# Microsoft Teams join link extraction and SafeLinks decoding
│   │   └── meeting_status.h / .cpp # Temporal calculation (Live, Soon, Future, Past)
│   ├── mod/                        # Windhawk mod lifecycle and shell integration
│   │   ├── mod_settings.h / .cpp   # Mod settings parsing and validation
│   │   ├── calendar_cache.h / .cpp # Thread-safe in-memory cache (std::shared_mutex)
│   │   ├── http_fetcher.h / .cpp   # Background WinINet worker thread with timer loop
│   │   └── teams_calendar_flyout.h # Lifecycle hooks (Wh_ModInit, Wh_ModUninit)
│   └── ui/                         # WinUI / XAML presentation layer
│       ├── agenda_view_model.h / .cpp # ViewModels, color palettes, and chronological sorting
│       └── xaml_injector.h / .cpp  # Visual tree discovery and container management
├── tests/                          # Unit and integration test suites
│   ├── run_tests.ps1               # Automated test runner via in-process DLL
│   ├── test_ics_parser.cpp         # RFC 5545 parsing and recurrence tests
│   ├── test_teams_extractor.cpp    # Link extraction and temporal status tests
│   ├── test_mod_skeleton_http.cpp  # Multithreaded cache and lifecycle tests
│   ├── test_agenda_ui.cpp          # ViewModel and Fluent UI tests
│   └── test_one_click_join_guide.cpp # URL sanitization, stability assertions, and bundle tests
└── scripts/
    └── generate_bundle.ps1         # Script bundling src/ into teams-calendar-flyout.wh.cpp
```

---

## 🛠️ How to Install and Use in Windhawk

### Prerequisites
1. Windows 11 (build 22H2 or higher).
2. [Windhawk](https://windhawk.net/) installed.

### Step 1: Obtain Your Calendar ICS Link
1. Open **Outlook on the Web** ([outlook.office.com](https://outlook.office.com)).
2. Click the gear icon (**Settings**) in the top right corner.
3. Navigate to **Calendar** ➔ **Shared calendars**.
4. Under **Publish a calendar**, select your calendar, choose permissions (*"Can view all details"*), and click **Publish**.
5. Copy the generated **ICS link** (begins with `https://outlook.office365.com/.../reachcalendar.ics`).

### Step 2: Load the Mod into Windhawk
1. Open the **Windhawk** application.
2. In the bottom-left corner, switch to or enable **Developer Mode**.
3. Click **Create Mod** (or **+ New Mod**).
4. In the code editor tab, replace all default code with the contents of [`teams-calendar-flyout.wh.cpp`](./teams-calendar-flyout.wh.cpp).
5. Click **Save & Compile**.
6. Switch to the **Settings** tab of the mod in Windhawk:
   - **Outlook / Teams ICS Feed URL**: Paste the ICS link copied in Step 1.
   - **Refresh Interval (minutes)**: Update frequency (default: 15 minutes).
   - **Show Past Events**: Check if you want completed meetings from earlier today to remain visible.
   - **Enable One-Click Join Button**: Keeps the direct join button active.
7. Click **Accept** to save settings.

Open your Windows 11 clock flyout (by clicking the taskbar clock) to view your upcoming Teams agenda!

For an illustrated walkthrough with troubleshooting, see [`TUTORIAL_MOD_WINDHAWK.md`](./TUTORIAL_MOD_WINDHAWK.md).

---

## 🧪 Running Automated Tests

The project includes over 30 unit and integration tests covering all layers without requiring active desktop windows or live UI sessions.

To run the entire test suite in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File tests/run_tests.ps1 -Suite all
```

You can also run individual test suites:
- `-Suite ics`: Tests the RFC 5545 parser, unfolding, and RRULE engine.
- `-Suite teams`: Tests URL extraction, SafeLinks decoding, and status calculations.
- `-Suite mod`: Tests settings validation, thread-safe cache, and the WinINet worker.
- `-Suite ui`: Tests WinUI XAML formatting and ViewModels.
- `-Suite join`: Tests URL sanitization, stability assertions, and bundle integrity.

---

## 🔒 Security, Privacy & Shell Stability

- **Direct HTTPS Connection**: Connects directly between your local machine and Microsoft 365 / Outlook endpoints. No intermediary servers, third-party proxies, or cloud relays.
- **Protocol Sanitization**: All meeting URLs are validated against a strict protocol allowlist (`https://`, `http://`, `msteams:`), preventing command injection or execution of arbitrary URI schemes.
- **Credential-Free**: Uses published iCalendar tokens generated by your account, eliminating the need to store passwords or access tokens locally.
- **Taskbar Process Isolation**: The mod injects exclusively into the Windows 11 flyout hosts (`ShellHost.exe` / `ShellExperienceHost.exe`). It intentionally **never** injects into `explorer.exe`, eliminating hooks or worker threads in the main taskbar process to prevent shell crashes or restarts.

---

## 📄 License

Distributed under the MIT License. See [`LICENSE`](LICENSE) for details.
