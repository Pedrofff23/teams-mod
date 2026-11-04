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

#include "src/mod/teams_calendar_flyout.wh.cpp"
