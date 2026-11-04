# Spec 0001: Microsoft Teams Calendar Integration for Windows 11 Calendar Flyout (Windhawk Mod)

## Problem Statement

In Windows 11, Microsoft redesigned the taskbar and clock flyout, removing the integrated agenda and calendar events view that previously existed in Windows 10. Users who rely on Microsoft Teams for their daily schedule cannot view upcoming meetings at a glance when clicking the taskbar clock/calendar. To verify schedules, check upcoming appointments, or join a call, users are forced to disrupt their focus, open the full Microsoft Teams desktop app or web portal, and navigate to the calendar view.

## Solution

A Windows mod built on the Windhawk platform that injects a dedicated Microsoft Teams agenda into the Windows 11 notification center and calendar flyout (`ShellHost.exe` / `ShellExperienceHost.exe`). The mod fetches calendar events via an Outlook/Teams `.ics` subscription feed, parses events, identifies Teams meeting invitations, and renders an interactive, styled agenda list directly below the calendar grid. For any meeting with a Teams join link, the mod provides a one-click "Join Meeting" action that launches the meeting directly in the Microsoft Teams client or browser, alongside visual status indicators ("Live / Happening Now", "Starting Soon").

## User Stories

1. As a Windows 11 user, I want to see my upcoming Microsoft Teams meetings when I open the taskbar calendar flyout, so that I don't have to keep the heavy Teams app open to check my schedule.
2. As a meeting attendee, I want to see the title, start time, end time, and location of each meeting, so that I have immediate context on my day.
3. As a busy professional, I want meetings that contain a Microsoft Teams link to display a one-click "Join Meeting" button, so that I can hop into calls immediately from my taskbar without opening emails or searching for meeting links.
4. As a user with multiple events, I want meetings currently in progress to be visually highlighted as "Live / In Progress", so that I immediately know which meeting is happening right now.
5. As a user preparing for my day, I want meetings starting within the next 15 minutes to display an "Upcoming / Soon" badge, so that I am never caught unprepared or late.
6. As a user with recurring standups and weekly syncs, I want recurring calendar rules to be parsed accurately, so that my repeating Teams calls show up on their respective days.
7. As a privacy-conscious user, I want my calendar feed URL to be stored securely in the local mod settings without requiring plain-text credentials or third-party cloud intermediaries.
8. As an administrator or developer, I want the mod to run safely within the Windows Shell process without causing crashes, memory leaks, or freezes in `explorer.exe` or `ShellHost.exe`.
9. As a user navigating past and future dates, I want the agenda list to dynamically filter and display the events for whichever date I select on the calendar month grid.
10. As a user who configures mods in Windhawk, I want intuitive configuration settings (such as Feed URL, sync refresh interval, and visual customization options) exposed through the Windhawk GUI settings panel.
11. As a user with intermittent network connectivity, I want previously fetched meetings to be cached locally in memory so that the calendar flyout still displays my schedule even if offline.
12. As a user attending hybrid meetings, I want physical room locations and virtual Teams links to both be clearly distinguished in the agenda item.
13. As a user with meetings spanning multiple hours, I want all-day and multi-hour events to display accurate duration bars or time ranges.
14. As a developer learning Windhawk modding, I want the mod to follow Windhawk best practices and clean module boundaries, so that I can maintain, adapt, and build further Windows shell modifications.

## Implementation Decisions

- **Mod Architecture & Injection Target**: The mod is implemented as a single C++ dynamic library compiled via Windhawk's Clang toolchain, targeting `ShellExperienceHost.exe` and `ShellHost.exe` on Windows 11.
- **UI Injection Seam**: WinUI / XAML visual tree manipulation. Hooks into the calendar container element (`CalendarCenterGrid`) to append or substitute a dedicated Teams agenda panel beneath the calendar grid.
- **Data Ingestion Seam**: Periodic HTTP/HTTPS client fetching an RFC 5545 iCalendar (`.ics`) feed from Outlook/Teams Web Calendar publishing endpoints.
- **Meeting Link Extraction**: Regex and URL pattern matching recognizing `https://teams.microsoft.com/l/meetup-join/...` and related Microsoft Teams join links from event body descriptions, summary, and location attributes.
- **Action Dispatch**: One-click join launches the system handler via standard Windows Shell API (`ShellExecuteW`), ensuring the system automatically routes to the native Microsoft Teams desktop application or default web browser.
- **Configuration Management**: Expose settings via `// ==WindhawkModSettings==` metadata block for URL entry, polling interval (e.g. 15 minutes), and appearance toggles.
- **Mod Lifecycle**: Standard Windhawk lifecycle callbacks (`Wh_ModInit`, `Wh_ModSettingsChanged`, `Wh_ModUninit`) ensuring clean tear-down and hot-reloading without leaving dangling hooks or memory leaks in `ShellHost.exe`.

## Testing Decisions

- **What makes a good test**: Tests must verify external behavior and contract fulfillment rather than private implementation details. A good test supplies an input (e.g. raw ICS data with time boundaries) and asserts the observable output (e.g. parsed events, detected Teams join URLs, correct date grouping) across clear boundaries.
- **Core Seam for Automated Testing**: The Calendar Engine (RFC 5545 parser, recurrence expansion, timezone calculations, and Teams meeting URL extraction) is cleanly decoupled from the Shell/XAML UI injection layer. This allows full unit testing headlessly via a CLI test harness without attaching to `ShellHost.exe` or requiring a live Windows UI session.
- **Integration Seam for Shell UI**: Verification of the XAML visual tree modification and user interaction is performed inside the Windhawk Developer Mode environment. Tests verify visual bounds in `CalendarCenterGrid`, theme adaptability (Dark/Light mode), and click dispatch to `ShellExecuteW`.
- **Prior Art**: Based on the proven architecture and reliability testing of the official `agenda-in-calendar-view.wh.cpp` community mod in the `ramensoftware/windhawk-mods` ecosystem.

## Out of Scope

- Writing, modifying, or creating events back to Microsoft Teams or Outlook (this is a read-only agenda view).
- In-process interactive OAuth2/MSAL authentication webviews inside `ShellHost.exe` (feed-based `.ics` sync is used instead for stability and security).
- Chat, channels, or presence status indicators (e.g. Busy/Available presence dots) outside the calendar flyout.
- Windows 10 support (specifically designed for Windows 11 WinUI/XAML calendar architecture).

## Further Notes

- References the proven open-source architecture of `agenda-in-calendar-view.wh.cpp` by the Windhawk community.
- Windows 11 AppContainer permissions: Local `.ics` files require read access granted to `ALL_APPLICATION_PACKAGES` (`*S-1-15-2-1`), though remote HTTPS URLs bypass local filesystem permissions.

