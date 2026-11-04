# 03: Windhawk Mod Skeleton, Settings Schema and Background HTTP Fetcher

**What to build:** The compilable C++ Windhawk mod architecture targeting Windows 11 shell host processes (`ShellExperienceHost.exe` / `ShellHost.exe`). Includes declarative metadata comments, customizable mod settings (Feed URL, sync interval, display toggles) accessible in Windhawk's configuration UI, and an asynchronous HTTP/HTTPS background worker using native Windows networking APIs to fetch and periodically refresh the calendar feed into memory.

**Blocked by:** 02: Microsoft Teams Meeting URL Extractor and Status Calculator

**Status:** ready-for-human

- [x] Complete Windhawk mod metadata block with `@id`, `@name`, `@description`, and target process `@include` definitions
- [x] Windhawk settings block exposing feed URL, polling refresh interval in minutes, and visual preference flags
- [x] Lifecycle hooks implementation (`Wh_ModInit`, `Wh_ModSettingsChanged`, `Wh_ModUninit`) with clean resource management
- [x] Asynchronous background HTTPS fetcher retrieving remote ICS data via Windows networking without blocking or freezing shell threads
- [x] Safe in-memory caching of the latest successful calendar snapshot for offline resilience
