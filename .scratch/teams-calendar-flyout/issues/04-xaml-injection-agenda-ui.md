# 04: Windows 11 Calendar Flyout XAML Injection and Agenda UI

**What to build:** Visual integration inside the Windows 11 calendar flyout. Intercepts the XAML visual tree of the calendar flyout within `ShellHost.exe`, locating the calendar center container (`CalendarCenterGrid`) and injecting a styled WinUI/XAML agenda panel beneath the calendar grid. Displays meeting cards with start/end time, subject, location, distinctive Teams branding, and dynamic status badges, updating immediately when the user selects different dates on the calendar.

**Blocked by:** 03: Windhawk Mod Skeleton, Settings Schema and Background HTTP Fetcher

**Status:** ready-for-human

- [x] Successful interception and hook into the Windows 11 calendar XAML visual tree
- [x] Safe injection and layout bounds handling inside `CalendarCenterGrid` preventing overflow or visual clipping
- [x] Rich card presentation for each event showing title, time span, physical/virtual location, and Microsoft Teams visual badge
- [x] Real-time visual status cues for ongoing ("Live / Now") and impending ("Starting Soon") meetings
- [x] Responsive date change binding: clicking another day on the calendar grid immediately updates the agenda list for that date
- [x] Seamless support for both Windows 11 Dark and Light system themes
