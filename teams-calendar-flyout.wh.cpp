// ==WindhawkMod==
// @id              teams-calendar-flyout
// @name            Microsoft Teams Calendar Flyout
// @description     Displays upcoming Microsoft Teams meetings in the Windows 11 calendar flyout
// @version         1.0.0
// @author          Pedrofff23
// @github          https://github.com/Pedrofff23/teams-mod
// @include         ShellHost.exe
// @include         ShellExperienceHost.exe
// @compilerOptions -lwininet -lole32 -lshell32
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
#ifdef WH_MOD
#include <windhawk_api.h>
#ifndef WH_MOD_ID
#define WH_MOD_ID L"teams-calendar-flyout"
#endif
#include <windhawk_utils.h>
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

// --- Header: src/calendar_engine/meeting_status.h ---

#include <string_view>
#include <cstdint>
#include <vector>

namespace calendar {

// Forward declarations
struct CalendarDateTime;
struct CalendarEvent;

enum class MeetingTemporalStatus {
    Past,
    Live,
    Soon,
    Future
};

std::string_view to_string(MeetingTemporalStatus status);

class MeetingStatusCalculator {
public:
    static constexpr int64_t kStartingSoonWindowSeconds = 15 * 60; // 900 seconds

    // Compute temporal status based on Unix timestamps in seconds
    static MeetingTemporalStatus CalculateStatus(int64_t start_unix, int64_t end_unix, int64_t now_unix);

    // Compute temporal status for a CalendarEvent relative to a reference CalendarDateTime
    static MeetingTemporalStatus CalculateStatus(const CalendarEvent& event, const CalendarDateTime& reference_time);

    // Enrich a single CalendarEvent with Teams detection and MeetingTemporalStatus
    static void EnrichEvent(CalendarEvent& event, const CalendarDateTime& reference_time);

    // Enrich multiple CalendarEvents with Teams detection and MeetingTemporalStatus
    static void EnrichEvents(std::vector<CalendarEvent>& events, const CalendarDateTime& reference_time);
};

} // namespace calendar


// --- Header: src/calendar_engine/ics_parser.h ---

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>
#include <compare>

namespace calendar {

struct CalendarDate {
    int year{0};
    int month{0};   // 1 - 12
    int day{0};     // 1 - 31

    auto operator<=>(const CalendarDate&) const = default;
    bool operator==(const CalendarDate&) const = default;

    // Returns day of week: 0 = Sunday, 1 = Monday, ..., 6 = Saturday
    int day_of_week() const;

    std::string to_string() const;
    static std::optional<CalendarDate> from_string(std::string_view str);
};

struct CalendarTime {
    int hour{0};    // 0 - 23
    int minute{0};  // 0 - 59
    int second{0};  // 0 - 59

    auto operator<=>(const CalendarTime&) const = default;
    bool operator==(const CalendarTime&) const = default;

    std::string to_string() const;
};

struct CalendarDateTime {
    CalendarDate date;
    CalendarTime time;
    bool is_utc{false};
    std::string tzid;

    auto operator<=>(const CalendarDateTime&) const = default;
    bool operator==(const CalendarDateTime&) const = default;

    // Convert to seconds since Unix epoch (1970-01-01 00:00:00 UTC)
    int64_t to_unix_timestamp() const;

    // Construct CalendarDateTime from seconds since Unix epoch
    static CalendarDateTime from_unix_timestamp(int64_t timestamp_seconds, bool is_utc = true);

    // Current local time
    static CalendarDateTime now_local(std::optional<int> tz_offset_minutes = std::nullopt);

    // Normalize to local time using offset in minutes
    CalendarDateTime to_local(int local_offset_minutes) const;

    std::string to_string() const;
};

enum class EventStatus {
    Unknown,
    Confirmed,
    Tentative,
    Cancelled
};

enum class RecurrenceFrequency {
    None,
    Daily,
    Weekly,
    Monthly,
    Yearly
};

enum class Weekday {
    Sunday = 0,
    Monday = 1,
    Tuesday = 2,
    Wednesday = 3,
    Thursday = 4,
    Friday = 5,
    Saturday = 6
};

struct RecurrenceRule {
    RecurrenceFrequency freq{RecurrenceFrequency::None};
    int interval{1};
    std::optional<int> count;
    std::optional<CalendarDateTime> until;
    std::vector<Weekday> by_day;

    bool is_recurring() const { return freq != RecurrenceFrequency::None; }
    bool matches(const CalendarDateTime& dtstart, const CalendarDate& target_date) const;
    static RecurrenceRule Parse(std::string_view rrule_str, int local_tz_offset_minutes = 0);
};

struct CalendarEvent {
    std::string uid;
    std::string summary;
    std::string description;
    std::string location;
    CalendarDateTime start_time;
    CalendarDateTime end_time;
    bool is_all_day{false};
    EventStatus status{EventStatus::Confirmed};
    bool is_teams_meeting{false};
    std::string teams_join_url;
    MeetingTemporalStatus temporal_status{MeetingTemporalStatus::Future};
    RecurrenceRule rrule;

    // Enrich event with Teams URL extraction and temporal status calculation
    void enrich_teams_and_status(const CalendarDateTime& ref_time);

    // Returns whether this event occurs on the given date (supporting recurring events)
    bool occurs_on(const CalendarDate& target_date) const;

    // Generates an occurrence instance of this event on target_date
    CalendarEvent generate_instance_for_date(const CalendarDate& target_date) const;
};

class IcsParser {
public:
    // Construct parser with optional fixed local timezone offset (in minutes from UTC).
    // If std::nullopt, the current system timezone offset is detected and used.
    explicit IcsParser(std::optional<int> local_tz_offset_minutes = std::nullopt);

    // RFC 5545 Line unfolding: joins lines starting with a space or tab to the previous line
    static std::string UnfoldLines(std::string_view ics_content);

    // RFC 5545 text unescaping: \, -> , ; \; -> ; ; \\ -> \ ; \N / \n -> newline
    static std::string UnescapeText(std::string_view escaped_text);

    // Parse a date or datetime string according to RFC 5545 specifications
    CalendarDateTime ParseDateTime(std::string_view dt_str, std::string_view params = "") const;

    // Static version of ParseDateTime for utility usage
    static CalendarDateTime ParseDateTimeStatic(std::string_view dt_str, std::string_view params = "");

    // Parse full RFC 5545 iCalendar content into a list of normalized CalendarEvents
    std::vector<CalendarEvent> Parse(std::string_view ics_content) const;

    // Query and sort events for a target date chronologically
    std::vector<CalendarEvent> GetEventsForDate(const std::vector<CalendarEvent>& events, const CalendarDate& date) const;

    // Get the configured or resolved local timezone offset in minutes
    int GetLocalTimezoneOffsetMinutes() const;

    // Helper to determine timezone offset from known TZID or Windows timezone names
    static int ResolveTzidOffsetMinutes(std::string_view tzid);

private:
    int local_tz_offset_minutes_{0};
};

} // namespace calendar


// --- Header: src/calendar_engine/teams_extractor.h ---

#include <string>
#include <string_view>
#include <optional>

namespace calendar {

struct CalendarEvent;

class TeamsExtractor {
public:
    // Extract a direct Microsoft Teams join URL from arbitrary text (plain text, HTML, etc.)
    static std::string ExtractTeamsUrl(std::string_view text);

    // Extract a Microsoft Teams join URL from an event (checks location, description, url fields)
    static std::string ExtractFromEvent(const CalendarEvent& event);

    // Check if a URL is a valid Microsoft Teams meeting URL
    static bool IsTeamsUrl(std::string_view url);

    // URL-decode a percent-encoded string
    static std::string UrlDecode(std::string_view encoded);

    // Decode an Outlook SafeLinks URL and extract the underlying target URL if present
    static std::optional<std::string> DecodeSafeLink(std::string_view safelink_url);
};

} // namespace calendar


// --- Header: src/mod/mod_settings.h ---

#include <string>
#include <string_view>
#include <compare>

namespace calendar {

struct ModSettings {
    static constexpr int kDefaultRefreshIntervalMinutes = 15;
    static constexpr int kMinimumRefreshIntervalMinutes = 1;

    std::wstring feed_url;
    int refresh_interval_minutes{kDefaultRefreshIntervalMinutes};
    bool show_past_events{false};
    bool enable_join_button{true};

    auto operator<=>(const ModSettings&) const = default;
    bool operator==(const ModSettings&) const = default;

    // Validate and normalize settings (clamps refresh interval, trims whitespace)
    void Normalize();

    // Whether a non-empty feed URL is configured
    bool IsFeedUrlConfigured() const;

    // Get the feed URL converted to UTF-8
    std::string GetFeedUrlUtf8() const;

    // Create ModSettings from explicit values
    static ModSettings FromValues(
        std::wstring_view url,
        int refresh_minutes = kDefaultRefreshIntervalMinutes,
        bool show_past = false,
        bool enable_join = true
    );

    static ModSettings FromValuesUtf8(
        std::string_view url,
        int refresh_minutes = kDefaultRefreshIntervalMinutes,
        bool show_past = false,
        bool enable_join = true
    );

    // Read current settings from Windhawk API (if in mod context)
    void LoadFromWindhawk();
};

} // namespace calendar


// --- Header: src/mod/calendar_cache.h ---

#include <vector>
#include <string>
#include <string_view>
#include <chrono>
#include <shared_mutex>
#include <functional>

namespace calendar {

enum class FetchStatus {
    Idle,
    Fetching,
    Succeeded,
    Failed
};

std::string_view to_string(FetchStatus status);

class CalendarCache {
public:
    using UpdateCallback = std::function<void()>;

    CalendarCache() = default;
    ~CalendarCache() = default;

    // Disallow copy/move to protect the synchronization primitives
    CalendarCache(const CalendarCache&) = delete;
    CalendarCache& operator=(const CalendarCache&) = delete;
    CalendarCache(CalendarCache&&) = delete;
    CalendarCache& operator=(CalendarCache&&) = delete;

    // Update fetch status. When Failed, error_message is saved while existing events are preserved
    void SetFetchStatus(FetchStatus status, std::string_view error_message = "");

    // Atomically replace cached events, setting status to Succeeded and updating timestamp
    void UpdateEvents(std::vector<CalendarEvent> new_events);

    // Retrieve a thread-safe snapshot copy of all cached events
    std::vector<CalendarEvent> GetEventsSnapshot() const;

    // Retrieve events occurring on target_date, optionally filtering out past events
    std::vector<CalendarEvent> GetEventsForDate(const CalendarDate& target_date, bool show_past_events = true) const;

    // Current fetch status
    FetchStatus GetStatus() const;

    // Last recorded error message (empty if none or succeeded)
    std::string GetLastError() const;

    // Timestamp of the last successful fetch update
    std::chrono::system_clock::time_point GetLastUpdatedTime() const;

    // Total number of events currently cached
    size_t EventCount() const;

    // Whether any events are cached
    bool HasEvents() const;

    // Reset the cache to initial empty state
    void Clear();

    // Register callback invoked when events are updated
    void SetOnUpdatedCallback(UpdateCallback cb);

private:
    mutable std::shared_mutex mutex_;
    std::vector<CalendarEvent> events_;
    FetchStatus status_{FetchStatus::Idle};
    std::string last_error_;
    std::chrono::system_clock::time_point last_updated_time_{};
    UpdateCallback on_updated_cb_;
};

} // namespace calendar


// --- Header: src/mod/http_fetcher.h ---

#include <string>
#include <string_view>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace calendar {

struct HttpResponse {
    int status_code{0};
    std::string body;
    std::string error_message;

    bool is_success() const { return status_code >= 200 && status_code < 300; }
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResponse Fetch(const std::wstring& url) = 0;
};

// Production HTTP transport using WinINet (InternetOpenW, InternetOpenUrlW, InternetReadFile)
class WinINetHttpTransport : public IHttpTransport {
public:
    WinINetHttpTransport();
    ~WinINetHttpTransport() override;

    HttpResponse Fetch(const std::wstring& url) override;
};

// Mock HTTP transport for unit and offline tests
class MockHttpTransport : public IHttpTransport {
public:
    MockHttpTransport() = default;

    void SetResponse(int status_code, std::string body, std::string error_message = "");

    HttpResponse Fetch(const std::wstring& url) override;

    int GetFetchCount() const;
    std::wstring GetLastRequestedUrl() const;

private:
    mutable std::mutex mutex_;
    int status_code_{200};
    std::string body_;
    std::string error_message_;
    int fetch_count_{0};
    std::wstring last_requested_url_;
};

class HttpFetcher {
public:
    explicit HttpFetcher(
        std::shared_ptr<CalendarCache> cache,
        std::shared_ptr<IHttpTransport> transport = nullptr
    );
    ~HttpFetcher();

    // Prevent copy/move
    HttpFetcher(const HttpFetcher&) = delete;
    HttpFetcher& operator=(const HttpFetcher&) = delete;

    // Start background polling worker thread with initial settings
    void Start(const ModSettings& settings);

    // Stop background worker thread cleanly
    void Stop();

    // Dynamically update settings (if URL changed or interval changed)
    void UpdateSettings(const ModSettings& settings);

    // Trigger an immediate fetch in the background thread
    void TriggerFetch();

    // Execute a synchronous fetch of the specified URL (useful for direct testing or explicit triggers)
    bool FetchOnce(const std::wstring& url);

    // Check if background worker thread is running
    bool IsRunning() const;

private:
    void WorkerLoop();

    std::shared_ptr<CalendarCache> cache_;
    std::shared_ptr<IHttpTransport> transport_;

    std::thread worker_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_requested_{false};
    bool refresh_requested_{false};
    bool is_running_{false};

    ModSettings current_settings_;
};

} // namespace calendar


// --- Header: src/ui/agenda_view_model.h ---

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace calendar {

// Forward declaration
class CalendarCache;

enum class ThemeMode {
    Dark,
    Light,
    HighContrast
};

struct ThemePalette {
    std::string card_background;
    std::string card_border;
    std::string primary_text;
    std::string secondary_text;
    std::string teams_accent;
    std::string teams_accent_hover;
    std::string teams_accent_pressed;
    std::string live_badge_bg;
    std::string live_badge_fg;
    std::string soon_badge_bg;
    std::string soon_badge_fg;
    std::string upcoming_badge_bg;
    std::string upcoming_badge_fg;
    std::string past_badge_bg;
    std::string past_badge_fg;
    std::string empty_text;
    std::string header_text;

    static ThemePalette ForTheme(ThemeMode mode);
};

struct AgendaItemViewModel {
    std::string uid;
    std::string title;
    std::string formatted_time_range;
    std::string location;
    bool is_teams{false};
    std::string join_url;
    bool enable_join_button{true};
    MeetingTemporalStatus temporal_status{MeetingTemporalStatus::Future};
    std::string status_badge_label;
    std::string status_badge_bg;
    std::string status_badge_fg;
    ThemePalette palette;
    bool is_all_day{false};
    int64_t start_unix{0};
    int64_t end_unix{0};

    // Accessors for backward compatibility and clean syntax
    const std::string& card_bg() const { return palette.card_background; }
    const std::string& card_border() const { return palette.card_border; }
    const std::string& primary_text_color() const { return palette.primary_text; }
    const std::string& secondary_text_color() const { return palette.secondary_text; }
    const std::string& accent_color() const { return palette.teams_accent; }
    const std::string& accent_hover_color() const { return palette.teams_accent_hover; }
    const std::string& accent_pressed_color() const { return palette.teams_accent_pressed; }

    static AgendaItemViewModel FromCalendarEvent(
        const CalendarEvent& event,
        ThemeMode theme,
        const CalendarDateTime& reference_time,
        bool enable_join = true
    );
};

struct AgendaDayViewModel {
    CalendarDate selected_date;
    std::string formatted_date_header;
    std::vector<AgendaItemViewModel> items;
    std::string empty_state_message{"No meetings scheduled for this day"};
    ThemeMode theme{ThemeMode::Dark};

    bool is_empty() const { return items.empty(); }
    size_t total_meetings_count() const { return items.size(); }
    size_t teams_meetings_count() const;
};

class AgendaPresenter {
public:
    static AgendaDayViewModel PresentDay(
        const CalendarCache& cache,
        const CalendarDate& date,
        ThemeMode theme,
        const CalendarDateTime& reference_time,
        bool show_past_events = false,
        bool enable_join_button = true
    );

    static AgendaDayViewModel PresentEvents(
        const std::vector<CalendarEvent>& events,
        const CalendarDate& date,
        ThemeMode theme,
        const CalendarDateTime& reference_time,
        bool show_past_events = false,
        bool enable_join_button = true
    );

    static std::string FormatDateHeader(const CalendarDate& date);
    static std::string FormatTimeRange(const CalendarDateTime& start, const CalendarDateTime& end, bool is_all_day);
};

} // namespace calendar


// --- Header: src/ui/xaml_injector.h ---

#include <string>
#include <string_view>
#include <memory>
#include <mutex>
#include <optional>

namespace calendar {

// Abstract interface for host XAML tree manipulation
class IXamlHostAdapter {
public:
    virtual ~IXamlHostAdapter() = default;

    virtual bool FindTargetContainer(std::string_view container_name) = 0;
    virtual bool InjectElement(std::string_view container_name, std::string_view element_id, std::string_view xaml_content) = 0;
    virtual bool RemoveElement(std::string_view container_name, std::string_view element_id) = 0;
    virtual bool UpdateElement(std::string_view element_id, std::string_view xaml_content) = 0;
    virtual ThemeMode DetectThemeMode() const = 0;
    virtual bool OpenMeetingUrl(std::string_view url) = 0;
};

// Validates and sanitizes meeting join URLs to prevent command injection
class UrlSanitizer {
public:
    // Returns true if url has an approved protocol scheme (http, https, msteams) and contains no dangerous characters
    static bool IsSafeMeetingUrl(std::string_view url);

    // Trims whitespace and validates URL safety
    static bool SanitizeAndValidateUrl(std::string_view url, std::string* sanitized_out = nullptr);
};

// Generates WinUI / XAML markup strings for the agenda panel
class XamlMarkupGenerator {
public:
    static std::string EscapeXml(std::string_view text);
    static std::string GenerateAgendaXaml(const AgendaDayViewModel& viewModel, int maxHeight = 260);
};

// Default WinUI adapter that targets Windows 11 ShellHost.exe
class WinUiXamlHostAdapter : public IXamlHostAdapter {
public:
    WinUiXamlHostAdapter() = default;
    ~WinUiXamlHostAdapter() override = default;

    bool FindTargetContainer(std::string_view container_name) override;
    bool InjectElement(std::string_view container_name, std::string_view element_id, std::string_view xaml_content) override;
    bool RemoveElement(std::string_view container_name, std::string_view element_id) override;
    bool UpdateElement(std::string_view element_id, std::string_view xaml_content) override;
    ThemeMode DetectThemeMode() const override;
    bool OpenMeetingUrl(std::string_view url) override;
};

// Controller for Windows 11 Calendar Center Grid XAML visual tree injection
class XamlInjector {
public:
    static constexpr const char* kDefaultTargetContainer = "CalendarCenterGrid";
    static constexpr const char* kAgendaPanelElementId = "TeamsAgendaPanel";
    static constexpr int kDefaultMaxHeight = 260;

    XamlInjector();
    ~XamlInjector();

    // Prevent copy and move
    XamlInjector(const XamlInjector&) = delete;
    XamlInjector& operator=(const XamlInjector&) = delete;

    bool Initialize(
        std::shared_ptr<CalendarCache> cache,
        const ModSettings& settings,
        std::shared_ptr<IXamlHostAdapter> adapter = nullptr
    );

    void Shutdown();

    // Invoked when user clicks/selects a new date on the calendar grid
    void OnDateSelected(const CalendarDate& new_date, std::optional<CalendarDateTime> reference_time = std::nullopt);

    // Invoked when system theme changes (Dark / Light / HighContrast)
    void OnThemeChanged(ThemeMode new_theme);

    // Invoked when user updates settings (e.g. show_past_events, enable_join_button)
    void OnSettingsChanged(const ModSettings& new_settings);

    // Invoked when background calendar fetch finishes and cache is updated
    void OnEventsUpdated(std::optional<CalendarDateTime> reference_time = std::nullopt);

    // Invoked when user clicks a Join Meeting button
    bool HandleJoinButtonClick(std::string_view join_url);

    // URL validation helper
    static bool IsSafeMeetingUrl(std::string_view url) {
        return UrlSanitizer::IsSafeMeetingUrl(url);
    }

    // Accessors for state inspection and testing
    const CalendarDate& GetSelectedDate() const;
    ThemeMode GetCurrentTheme() const;
    bool IsInjected() const;
    std::string GetCurrentXaml() const;
    AgendaDayViewModel GetCurrentViewModel() const;

private:
    void RenderAndInjectLocked(std::optional<CalendarDateTime> reference_time);

    mutable std::mutex mutex_;
    std::shared_ptr<CalendarCache> cache_;
    std::shared_ptr<IXamlHostAdapter> adapter_;
    ModSettings settings_;
    CalendarDate selected_date_;
    ThemeMode current_theme_{ThemeMode::Dark};
    bool is_injected_{false};
    std::string current_xaml_;
    AgendaDayViewModel current_view_model_;
};

} // namespace calendar


// --- Header: src/mod/teams_calendar_flyout.h ---

#include <windows.h>
#include <memory>
#include <mutex>

namespace calendar {

class ModLifecycleManager {
public:
    static ModLifecycleManager& Instance();

    // Prevent copying and moving
    ModLifecycleManager(const ModLifecycleManager&) = delete;
    ModLifecycleManager& operator=(const ModLifecycleManager&) = delete;

    // Mod lifecycle operations
    bool Initialize();
    void OnSettingsChanged();
    void Uninitialize();

    bool IsInitialized() const;

    std::shared_ptr<CalendarCache> GetCache() const;
    std::shared_ptr<HttpFetcher> GetFetcher() const;
    std::shared_ptr<XamlInjector> GetInjector() const;
    ModSettings GetSettings() const;

    // Injection seam for testing
    void SetCustomTransport(std::shared_ptr<IHttpTransport> transport);
    void SetCustomXamlAdapter(std::shared_ptr<IXamlHostAdapter> adapter);
    void SetSettingsForTesting(const ModSettings& settings);
    void ResetForTesting();

private:
    ModLifecycleManager();
    ~ModLifecycleManager();

    mutable std::mutex mutex_;
    std::shared_ptr<CalendarCache> cache_;
    std::shared_ptr<HttpFetcher> fetcher_;
    std::shared_ptr<XamlInjector> injector_;
    std::shared_ptr<IHttpTransport> custom_transport_;
    std::shared_ptr<IXamlHostAdapter> custom_adapter_;
    ModSettings settings_;
    bool initialized_{false};
};

} // namespace calendar

// Windhawk mod entry points
extern "C" {
__declspec(dllexport) BOOL Wh_ModInit();
__declspec(dllexport) void Wh_ModSettingsChanged();
__declspec(dllexport) void Wh_ModUninit();
}


// --- Source: src/calendar_engine/meeting_status.cpp ---

namespace calendar {

std::string_view to_string(MeetingTemporalStatus status) {
    switch (status) {
        case MeetingTemporalStatus::Past: return "Past";
        case MeetingTemporalStatus::Live: return "Live";
        case MeetingTemporalStatus::Soon: return "Soon";
        case MeetingTemporalStatus::Future: return "Future";
    }
    return "Future";
}

MeetingTemporalStatus MeetingStatusCalculator::CalculateStatus(int64_t start_unix, int64_t end_unix, int64_t now_unix) {
    if (now_unix > end_unix) {
        return MeetingTemporalStatus::Past;
    }
    if (start_unix <= now_unix && now_unix <= end_unix) {
        return MeetingTemporalStatus::Live;
    }
    int64_t diff = start_unix - now_unix;
    if (diff > 0 && diff <= kStartingSoonWindowSeconds) {
        return MeetingTemporalStatus::Soon;
    }
    return MeetingTemporalStatus::Future;
}

MeetingTemporalStatus MeetingStatusCalculator::CalculateStatus(const CalendarEvent& event, const CalendarDateTime& reference_time) {
    int64_t now_unix = reference_time.to_unix_timestamp();

    if (event.is_all_day) {
        int64_t start_unix = event.start_time.to_unix_timestamp();
        // For RFC 5545 all day events, end_time is exclusive start of next day (or same day if unspecified)
        int64_t end_unix;
        if (event.end_time.date.year > 0 && event.end_time.date > event.start_time.date) {
            // End of previous day: DTEND - 1 second (23:59:59)
            end_unix = event.end_time.to_unix_timestamp() - 1;
        } else {
            // Single-day all-day event: 23:59:59 of start day
            CalendarDateTime end_day = event.start_time;
            end_day.time = CalendarTime{23, 59, 59};
            end_unix = end_day.to_unix_timestamp();
        }

        return CalculateStatus(start_unix, end_unix, now_unix);
    }

    int64_t start_unix = event.start_time.to_unix_timestamp();
    int64_t end_unix = event.end_time.to_unix_timestamp();
    return CalculateStatus(start_unix, end_unix, now_unix);
}

void MeetingStatusCalculator::EnrichEvent(CalendarEvent& event, const CalendarDateTime& reference_time) {
    event.enrich_teams_and_status(reference_time);
}

void MeetingStatusCalculator::EnrichEvents(std::vector<CalendarEvent>& events, const CalendarDateTime& reference_time) {
    for (auto& event : events) {
        EnrichEvent(event, reference_time);
    }
}

} // namespace calendar


// --- Source: src/calendar_engine/ics_parser.cpp ---

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <ctime>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace calendar {

namespace {

// Howard Hinnant's civil date to days since 1970-01-01
int64_t DaysFromCivil(int y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Convert days since 1970-01-01 to civil date
void CivilFromDays(int64_t z, int& year, int& month, int& day) noexcept {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int y = static_cast<int>(yoe) + static_cast<int>(era * 400);
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    year = y + (month <= 2);
}

std::string Trim(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

std::string ToUpper(std::string_view sv) {
    std::string s;
    s.reserve(sv.size());
    for (char c : sv) {
        s.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return s;
}

} // namespace

// ============================================================================
// CalendarDate
// ============================================================================

int CalendarDate::day_of_week() const {
    int64_t days = DaysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    int wd = static_cast<int>((days + 4) % 7);
    if (wd < 0) {
        wd += 7;
    }
    return wd;
}

std::string CalendarDate::to_string() const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    return std::string(buf);
}

std::optional<CalendarDate> CalendarDate::from_string(std::string_view str) {
    std::string s = Trim(str);
    if (s.size() == 10 && s[4] == '-' && s[7] == '-') {
        // YYYY-MM-DD
        CalendarDate cd;
        cd.year = std::stoi(s.substr(0, 4));
        cd.month = std::stoi(s.substr(5, 2));
        cd.day = std::stoi(s.substr(8, 2));
        return cd;
    }
    if (s.size() == 8) {
        // YYYYMMDD
        CalendarDate cd;
        cd.year = std::stoi(s.substr(0, 4));
        cd.month = std::stoi(s.substr(4, 2));
        cd.day = std::stoi(s.substr(6, 2));
        return cd;
    }
    return std::nullopt;
}

// ============================================================================
// CalendarTime
// ============================================================================

std::string CalendarTime::to_string() const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, minute, second);
    return std::string(buf);
}

// ============================================================================
// CalendarDateTime
// ============================================================================

int64_t CalendarDateTime::to_unix_timestamp() const {
    int64_t days = DaysFromCivil(date.year, static_cast<unsigned>(date.month), static_cast<unsigned>(date.day));
    return days * 86400 + static_cast<int64_t>(time.hour) * 3600 +
           static_cast<int64_t>(time.minute) * 60 + static_cast<int64_t>(time.second);
}

CalendarDateTime CalendarDateTime::from_unix_timestamp(int64_t timestamp_seconds, bool is_utc) {
    CalendarDateTime dt;
    dt.is_utc = is_utc;

    int64_t days = timestamp_seconds / 86400;
    int64_t rem = timestamp_seconds % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }

    CivilFromDays(days, dt.date.year, dt.date.month, dt.date.day);
    dt.time.hour = static_cast<int>(rem / 3600);
    rem %= 3600;
    dt.time.minute = static_cast<int>(rem / 60);
    dt.time.second = static_cast<int>(rem % 60);
    return dt;
}

CalendarDateTime CalendarDateTime::now_local(std::optional<int> tz_offset_minutes) {
    if (tz_offset_minutes.has_value()) {
        std::time_t t = std::time(nullptr);
        int64_t local_ts = static_cast<int64_t>(t) + static_cast<int64_t>(*tz_offset_minutes) * 60;
        return from_unix_timestamp(local_ts, false);
    }

#if defined(_WIN32)
    SYSTEMTIME st;
    GetLocalTime(&st);
    CalendarDateTime dt;
    dt.date = CalendarDate{st.wYear, st.wMonth, st.wDay};
    dt.time = CalendarTime{st.wHour, st.wMinute, st.wSecond};
    dt.is_utc = false;
    return dt;
#else
    std::time_t t = std::time(nullptr);
    std::tm tm_local{};
    localtime_r(&t, &tm_local);
    CalendarDateTime dt;
    dt.date = CalendarDate{tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday};
    dt.time = CalendarTime{tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec};
    dt.is_utc = false;
    return dt;
#endif
}

CalendarDateTime CalendarDateTime::to_local(int local_offset_minutes) const {
    if (is_utc) {
        int64_t utc_ts = to_unix_timestamp();
        int64_t local_ts = utc_ts + static_cast<int64_t>(local_offset_minutes) * 60;
        CalendarDateTime local_dt = from_unix_timestamp(local_ts, false);
        local_dt.tzid = "";
        return local_dt;
    }

    if (!tzid.empty()) {
        int source_offset = IcsParser::ResolveTzidOffsetMinutes(tzid);
        int64_t raw_ts = to_unix_timestamp();
        // Convert to UTC by subtracting source offset
        int64_t utc_ts = raw_ts - static_cast<int64_t>(source_offset) * 60;
        // Convert to target local time by adding local offset
        int64_t local_ts = utc_ts + static_cast<int64_t>(local_offset_minutes) * 60;
        CalendarDateTime local_dt = from_unix_timestamp(local_ts, false);
        local_dt.tzid = "";
        return local_dt;
    }

    // Floating local time
    return *this;
}

std::string CalendarDateTime::to_string() const {
    return date.to_string() + "T" + time.to_string() + (is_utc ? "Z" : "");
}

// ============================================================================
// RecurrenceRule
// ============================================================================

RecurrenceRule RecurrenceRule::Parse(std::string_view rrule_str, int local_tz_offset_minutes) {
    RecurrenceRule rule;
    std::string str(rrule_str);
    std::istringstream token_stream(str);
    std::string token;

    while (std::getline(token_stream, token, ';')) {
        size_t eq_pos = token.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        std::string key = ToUpper(Trim(token.substr(0, eq_pos)));
        std::string val = Trim(token.substr(eq_pos + 1));
        std::string upper_val = ToUpper(val);

        if (key == "FREQ") {
            if (upper_val == "DAILY") {
                rule.freq = RecurrenceFrequency::Daily;
            } else if (upper_val == "WEEKLY") {
                rule.freq = RecurrenceFrequency::Weekly;
            } else if (upper_val == "MONTHLY") {
                rule.freq = RecurrenceFrequency::Monthly;
            } else if (upper_val == "YEARLY") {
                rule.freq = RecurrenceFrequency::Yearly;
            }
        } else if (key == "INTERVAL") {
            try {
                rule.interval = std::max(1, std::stoi(val));
            } catch (...) {
                rule.interval = 1;
            }
        } else if (key == "COUNT") {
            try {
                rule.count = std::max(0, std::stoi(val));
            } catch (...) {
                rule.count = std::nullopt;
            }
        } else if (key == "UNTIL") {
            CalendarDateTime raw_until = IcsParser::ParseDateTimeStatic(val);
            rule.until = raw_until.to_local(local_tz_offset_minutes);
        } else if (key == "BYDAY") {
            std::istringstream day_stream(upper_val);
            std::string day_token;
            while (std::getline(day_stream, day_token, ',')) {
                day_token = Trim(day_token);
                if (day_token == "SU") rule.by_day.push_back(Weekday::Sunday);
                else if (day_token == "MO") rule.by_day.push_back(Weekday::Monday);
                else if (day_token == "TU") rule.by_day.push_back(Weekday::Tuesday);
                else if (day_token == "WE") rule.by_day.push_back(Weekday::Wednesday);
                else if (day_token == "TH") rule.by_day.push_back(Weekday::Thursday);
                else if (day_token == "FR") rule.by_day.push_back(Weekday::Friday);
                else if (day_token == "SA") rule.by_day.push_back(Weekday::Saturday);
            }
        }
    }

    return rule;
}

bool RecurrenceRule::matches(const CalendarDateTime& dtstart, const CalendarDate& target_date) const {
    if (!is_recurring()) {
        return false;
    }

    const CalendarDate& start_date = dtstart.date;
    if (target_date < start_date) {
        return false;
    }

    if (until.has_value()) {
        if (target_date > until->date) {
            return false;
        }
        if (target_date == until->date) {
            if (until->time.hour != 0 || until->time.minute != 0 || until->time.second != 0) {
                if (dtstart.time > until->time) {
                    return false;
                }
            }
        }
    }

    int eff_interval = std::max(1, interval);

    if (freq == RecurrenceFrequency::Daily) {
        int64_t diff_days = DaysFromCivil(target_date.year, static_cast<unsigned>(target_date.month), static_cast<unsigned>(target_date.day)) -
                            DaysFromCivil(start_date.year, static_cast<unsigned>(start_date.month), static_cast<unsigned>(start_date.day));
        if (diff_days < 0 || (diff_days % eff_interval != 0)) {
            return false;
        }
        if (count.has_value()) {
            int64_t instance_idx = diff_days / eff_interval;
            if (instance_idx >= *count) {
                return false;
            }
        }
        return true;
    }

    if (freq == RecurrenceFrequency::Weekly) {
        int64_t days_start = DaysFromCivil(start_date.year, static_cast<unsigned>(start_date.month), static_cast<unsigned>(start_date.day));
        int wd_start = static_cast<int>((days_start + 4) % 7);
        if (wd_start < 0) wd_start += 7;
        int mon_offset_start = (wd_start == 0) ? 6 : (wd_start - 1);
        int64_t mon_start = days_start - mon_offset_start;

        int64_t days_target = DaysFromCivil(target_date.year, static_cast<unsigned>(target_date.month), static_cast<unsigned>(target_date.day));
        int wd_target = static_cast<int>((days_target + 4) % 7);
        if (wd_target < 0) wd_target += 7;
        int mon_offset_target = (wd_target == 0) ? 6 : (wd_target - 1);
        int64_t mon_target = days_target - mon_offset_target;

        int64_t diff_weeks = (mon_target - mon_start) / 7;
        if (diff_weeks < 0) {
            return false;
        }
        if (diff_weeks % eff_interval != 0) {
            return false;
        }

        // Check if target weekday matches
        bool wd_matches = false;
        if (by_day.empty()) {
            wd_matches = (wd_target == wd_start);
        } else {
            for (Weekday wd : by_day) {
                if (static_cast<int>(wd) == wd_target) {
                    wd_matches = true;
                    break;
                }
            }
        }

        if (!wd_matches) {
            return false;
        }

        if (count.has_value()) {
            int occurrences = 0;
            for (int64_t w = 0; w <= diff_weeks; w += eff_interval) {
                int64_t curr_mon = mon_start + w * 7;
                for (int day_idx = 0; day_idx < 7; ++day_idx) {
                    int64_t curr_day = curr_mon + day_idx;
                    if (curr_day < days_start) continue;
                    if (curr_day > days_target) break;

                    int curr_wd = static_cast<int>((curr_day + 4) % 7);
                    if (curr_wd < 0) curr_wd += 7;

                    bool day_matches = false;
                    if (by_day.empty()) {
                        day_matches = (curr_wd == wd_start);
                    } else {
                        for (Weekday wd : by_day) {
                            if (static_cast<int>(wd) == curr_wd) {
                                day_matches = true;
                                break;
                            }
                        }
                    }

                    if (day_matches) {
                        occurrences++;
                        if (curr_day == days_target) {
                            return occurrences <= *count;
                        }
                        if (occurrences >= *count) {
                            return false;
                        }
                    }
                }
            }
            return false;
        }

        return true;
    }

    return false;
}

// ============================================================================
// CalendarEvent
// ============================================================================

bool CalendarEvent::occurs_on(const CalendarDate& target_date) const {
    if (rrule.is_recurring()) {
        return rrule.matches(start_time, target_date);
    }

    if (is_all_day) {
        if (end_time.date.year == 0 || end_time.date <= start_time.date) {
            return target_date == start_time.date;
        }
        // RFC 5545: End date is exclusive for all-day events
        return (start_time.date <= target_date && target_date < end_time.date);
    }

    // Timed event
    if (start_time.date == target_date) {
        return true;
    }

    // Check multi-day timed event
    if (start_time.date <= target_date && target_date <= end_time.date) {
        // If end_time ends exactly at 00:00:00 of end_time.date, it does not include that day
        if (target_date == end_time.date &&
            end_time.time.hour == 0 && end_time.time.minute == 0 && end_time.time.second == 0) {
            return false;
        }
        return true;
    }

    return false;
}

CalendarEvent CalendarEvent::generate_instance_for_date(const CalendarDate& target_date) const {
    CalendarEvent instance = *this;
    instance.start_time.date = target_date;

    if (is_all_day) {
        int64_t start_days = DaysFromCivil(start_time.date.year, static_cast<unsigned>(start_time.date.month), static_cast<unsigned>(start_time.date.day));
        int64_t end_days = DaysFromCivil(end_time.date.year, static_cast<unsigned>(end_time.date.month), static_cast<unsigned>(end_time.date.day));
        int64_t duration_days = end_days - start_days;
        if (duration_days <= 0) {
            duration_days = 1;
        }
        int64_t new_end_days = DaysFromCivil(target_date.year, static_cast<unsigned>(target_date.month), static_cast<unsigned>(target_date.day)) + duration_days;
        CivilFromDays(new_end_days, instance.end_time.date.year, instance.end_time.date.month, instance.end_time.date.day);
        instance.end_time.time = {0, 0, 0};
    } else {
        int64_t start_ts = start_time.to_unix_timestamp();
        int64_t end_ts = end_time.to_unix_timestamp();
        int64_t duration_secs = end_ts - start_ts;
        if (duration_secs <= 0) {
            duration_secs = 3600; // default 1 hour
        }
        int64_t new_start_ts = instance.start_time.to_unix_timestamp();
        instance.end_time = CalendarDateTime::from_unix_timestamp(new_start_ts + duration_secs, instance.start_time.is_utc);
        instance.end_time.tzid = instance.start_time.tzid;
    }

    return instance;
}

void CalendarEvent::enrich_teams_and_status(const CalendarDateTime& ref_time) {
    std::string detected = TeamsExtractor::ExtractFromEvent(*this);
    if (!detected.empty()) {
        is_teams_meeting = true;
        teams_join_url = detected;
    } else {
        is_teams_meeting = false;
        teams_join_url.clear();
    }
    temporal_status = MeetingStatusCalculator::CalculateStatus(*this, ref_time);
}

// ============================================================================
// IcsParser
// ============================================================================

IcsParser::IcsParser(std::optional<int> local_tz_offset_minutes) {
    if (local_tz_offset_minutes.has_value()) {
        local_tz_offset_minutes_ = *local_tz_offset_minutes;
    } else {
#ifdef _WIN32
        TIME_ZONE_INFORMATION tzi;
        DWORD res = GetTimeZoneInformation(&tzi);
        if (res != TIME_ZONE_ID_INVALID) {
            int bias = tzi.Bias;
            if (res == TIME_ZONE_ID_DAYLIGHT) {
                bias += tzi.DaylightBias;
            } else if (res == TIME_ZONE_ID_STANDARD) {
                bias += tzi.StandardBias;
            }
            local_tz_offset_minutes_ = -bias;
        } else {
            local_tz_offset_minutes_ = 0;
        }
#else
        local_tz_offset_minutes_ = 0;
#endif
    }
}

std::string IcsParser::UnfoldLines(std::string_view ics_content) {
    std::string result;
    result.reserve(ics_content.size());

    for (size_t i = 0; i < ics_content.size(); ++i) {
        // CRLF followed by space or tab
        if (ics_content[i] == '\r' && i + 2 < ics_content.size() &&
            ics_content[i + 1] == '\n' &&
            (ics_content[i + 2] == ' ' || ics_content[i + 2] == '\t')) {
            i += 2; // skip \r, \n and the folding whitespace (for loop will advance past it)
            continue;
        }
        // LF followed by space or tab
        if (ics_content[i] == '\n' && i + 1 < ics_content.size() &&
            (ics_content[i + 1] == ' ' || ics_content[i + 1] == '\t')) {
            i += 1; // skip \n and the folding whitespace
            continue;
        }
        result.push_back(ics_content[i]);
    }

    return result;
}

std::string IcsParser::UnescapeText(std::string_view escaped_text) {
    std::string result;
    result.reserve(escaped_text.size());

    for (size_t i = 0; i < escaped_text.size(); ++i) {
        if (escaped_text[i] == '\\' && i + 1 < escaped_text.size()) {
            char next = escaped_text[i + 1];
            if (next == '\\') {
                result.push_back('\\');
                ++i;
            } else if (next == ',') {
                result.push_back(',');
                ++i;
            } else if (next == ';') {
                result.push_back(';');
                ++i;
            } else if (next == 'n' || next == 'N') {
                result.push_back('\n');
                ++i;
            } else {
                result.push_back(escaped_text[i]);
            }
        } else {
            result.push_back(escaped_text[i]);
        }
    }

    return result;
}

CalendarDateTime IcsParser::ParseDateTime(std::string_view dt_str, std::string_view params) const {
    CalendarDateTime dt;
    std::string val = Trim(dt_str);

    // Extract TZID from parameters if present
    if (!params.empty()) {
        std::string upper_params = ToUpper(params);
        size_t tzid_pos = upper_params.find("TZID=");
        if (tzid_pos != std::string::npos) {
            size_t val_start = tzid_pos + 5;
            size_t val_end = params.find(';', val_start);
            if (val_end == std::string::npos) {
                val_end = params.size();
            }
            std::string tzid_val = std::string(params.substr(val_start, val_end - val_start));
            if (tzid_val.size() >= 2 && tzid_val.front() == '"' && tzid_val.back() == '"') {
                tzid_val = tzid_val.substr(1, tzid_val.size() - 2);
            }
            dt.tzid = tzid_val;
        }
    }

    if (val.empty()) {
        return dt;
    }

    if (val.back() == 'Z' || val.back() == 'z') {
        dt.is_utc = true;
        val.pop_back();
    }

    size_t t_pos = val.find('T');
    if (t_pos == std::string::npos) {
        t_pos = val.find('t');
    }

    if (t_pos != std::string::npos) {
        // Date part
        if (t_pos >= 8) {
            dt.date.year = std::stoi(val.substr(0, 4));
            dt.date.month = std::stoi(val.substr(4, 2));
            dt.date.day = std::stoi(val.substr(6, 2));
        }
        // Time part
        std::string time_part = val.substr(t_pos + 1);
        if (time_part.size() >= 2) {
            dt.time.hour = std::stoi(time_part.substr(0, 2));
        }
        if (time_part.size() >= 4) {
            dt.time.minute = std::stoi(time_part.substr(2, 2));
        }
        if (time_part.size() >= 6) {
            dt.time.second = std::stoi(time_part.substr(4, 2));
        }
    } else {
        // Date only (e.g. 20261104)
        if (val.size() >= 8) {
            dt.date.year = std::stoi(val.substr(0, 4));
            dt.date.month = std::stoi(val.substr(4, 2));
            dt.date.day = std::stoi(val.substr(6, 2));
        }
        dt.time = {0, 0, 0};
    }

    return dt;
}

CalendarDateTime IcsParser::ParseDateTimeStatic(std::string_view dt_str, std::string_view params) {
    IcsParser p(0);
    return p.ParseDateTime(dt_str, params);
}

int IcsParser::ResolveTzidOffsetMinutes(std::string_view tzid) {
    std::string id = ToUpper(Trim(tzid));

    // Common aliases and standard time zones (in minutes from UTC)
    static const std::unordered_map<std::string, int> tz_table = {
        {"UTC", 0},
        {"GMT", 0},
        {"ETC/UTC", 0},
        {"ETC/GMT", 0},
        {"GREENWICH STANDARD TIME", 0},

        // North America
        {"EASTERN STANDARD TIME", -300},
        {"AMERICA/NEW_YORK", -300},
        {"EST", -300},
        {"US/EASTERN", -300},

        {"CENTRAL STANDARD TIME", -360},
        {"AMERICA/CHICAGO", -360},
        {"CST", -360},
        {"US/CENTRAL", -360},

        {"MOUNTAIN STANDARD TIME", -420},
        {"AMERICA/DENVER", -420},
        {"MST", -420},
        {"US/MOUNTAIN", -420},

        {"PACIFIC STANDARD TIME", -480},
        {"AMERICA/LOS_ANGELES", -480},
        {"PST", -480},
        {"US/PACIFIC", -480},

        {"ALASKAN STANDARD TIME", -540},
        {"AMERICA/ANCHORAGE", -540},

        {"HAWAIIAN STANDARD TIME", -600},
        {"PACIFIC/HONOLULU", -600},

        // South America
        {"E. SOUTH AMERICA STANDARD TIME", -180},
        {"AMERICA/SAO_PAULO", -180},
        {"BRT", -180},

        // Europe
        {"GMT STANDARD TIME", 0},
        {"EUROPE/LONDON", 0},
        {"W. EUROPE STANDARD TIME", 60},
        {"CENTRAL EUROPEAN STANDARD TIME", 60},
        {"ROMANCE STANDARD TIME", 60},
        {"EUROPE/BERLIN", 60},
        {"EUROPE/PARIS", 60},
        {"CET", 60},

        {"FLE STANDARD TIME", 120},
        {"GTB STANDARD TIME", 120},
        {"EUROPE/ATHENS", 120},
        {"EUROPE/HELSINKI", 120},
        {"EET", 120},

        // Asia / Pacific
        {"INDIA STANDARD TIME", 330},
        {"ASIA/KOLKATA", 330},
        {"ASIA/CALCUTTA", 330},
        {"IST", 330},

        {"SINGAPORE STANDARD TIME", 480},
        {"CHINA STANDARD TIME", 480},
        {"ASIA/SINGAPORE", 480},
        {"ASIA/SHANGHAI", 480},

        {"TOKYO STANDARD TIME", 540},
        {"ASIA/TOKYO", 540},
        {"JST", 540},

        {"AUS EASTERN STANDARD TIME", 600},
        {"AUSTRALIA/SYDNEY", 600},
        {"CEN. AUSTRALIA STANDARD TIME", 570},
        {"AUSTRALIA/ADELAIDE", 570},

        {"NEW ZEALAND STANDARD TIME", 720},
        {"PACIFIC/AUCKLAND", 720}
    };

    auto it = tz_table.find(id);
    if (it != tz_table.end()) {
        return it->second;
    }

    return 0; // Default to UTC offset if unrecognized
}

std::vector<CalendarEvent> IcsParser::Parse(std::string_view ics_content) const {
    std::string unfolded = UnfoldLines(ics_content);
    std::vector<CalendarEvent> events;

    std::istringstream stream(unfolded);
    std::string line;

    bool in_vevent = false;
    int sub_component_depth = 0; // Tracks embedded components like VALARM inside VEVENT
    CalendarEvent current_event;

    while (std::getline(stream, line)) {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            continue;
        }

        if (line == "BEGIN:VEVENT") {
            in_vevent = true;
            sub_component_depth = 0;
            current_event = CalendarEvent{};
            continue;
        }

        if (in_vevent && line == "END:VEVENT") {
            // End of current VEVENT
            // If end_time not provided, default based on start_time
            if (current_event.end_time.date.year == 0) {
                if (current_event.is_all_day) {
                    current_event.end_time = current_event.start_time;
                    int64_t ts = current_event.start_time.to_unix_timestamp() + 86400;
                    current_event.end_time = CalendarDateTime::from_unix_timestamp(ts, false);
                } else {
                    int64_t ts = current_event.start_time.to_unix_timestamp() + 3600;
                    current_event.end_time = CalendarDateTime::from_unix_timestamp(ts, current_event.start_time.is_utc);
                }
            }

            // Normalize times to local time
            if (!current_event.is_all_day) {
                current_event.start_time = current_event.start_time.to_local(local_tz_offset_minutes_);
                current_event.end_time = current_event.end_time.to_local(local_tz_offset_minutes_);
            }

            std::string detected_teams = TeamsExtractor::ExtractFromEvent(current_event);
            if (!detected_teams.empty()) {
                current_event.is_teams_meeting = true;
                current_event.teams_join_url = detected_teams;
            } else {
                current_event.is_teams_meeting = false;
                current_event.teams_join_url.clear();
            }

            events.push_back(current_event);
            in_vevent = false;
            sub_component_depth = 0;
            continue;
        }

        if (!in_vevent) {
            continue;
        }

        // Handle nested components (e.g. BEGIN:VALARM ... END:VALARM)
        if (line.rfind("BEGIN:", 0) == 0) {
            sub_component_depth++;
            continue;
        }
        if (line.rfind("END:", 0) == 0) {
            if (sub_component_depth > 0) {
                sub_component_depth--;
            }
            continue;
        }

        // Skip properties of sub-components
        if (sub_component_depth > 0) {
            continue;
        }

        // Split line into Name[;Params] : Value
        size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }

        std::string prop_head = line.substr(0, colon_pos);
        std::string_view prop_value = std::string_view(line).substr(colon_pos + 1);

        std::string prop_name;
        std::string prop_params;

        size_t semi_pos = prop_head.find(';');
        if (semi_pos != std::string::npos) {
            prop_name = ToUpper(prop_head.substr(0, semi_pos));
            prop_params = prop_head.substr(semi_pos + 1);
        } else {
            prop_name = ToUpper(prop_head);
        }

        if (prop_name == "UID") {
            current_event.uid = Trim(prop_value);
        } else if (prop_name == "SUMMARY") {
            current_event.summary = UnescapeText(Trim(prop_value));
        } else if (prop_name == "DESCRIPTION") {
            current_event.description = UnescapeText(Trim(prop_value));
        } else if (prop_name == "LOCATION") {
            current_event.location = UnescapeText(Trim(prop_value));
        } else if (prop_name == "STATUS") {
            std::string st = ToUpper(Trim(prop_value));
            if (st == "CONFIRMED") {
                current_event.status = EventStatus::Confirmed;
            } else if (st == "TENTATIVE") {
                current_event.status = EventStatus::Tentative;
            } else if (st == "CANCELLED") {
                current_event.status = EventStatus::Cancelled;
            } else {
                current_event.status = EventStatus::Unknown;
            }
        } else if (prop_name == "DTSTART") {
            current_event.start_time = ParseDateTime(prop_value, prop_params);
            std::string upper_params = ToUpper(prop_params);
            if (upper_params.find("VALUE=DATE") != std::string::npos || Trim(prop_value).size() == 8) {
                current_event.is_all_day = true;
            }
        } else if (prop_name == "DTEND") {
            current_event.end_time = ParseDateTime(prop_value, prop_params);
        } else if (prop_name == "RRULE") {
            current_event.rrule = RecurrenceRule::Parse(prop_value, local_tz_offset_minutes_);
        } else if (prop_name == "URL" || prop_name == "X-MICROSOFT-SKYPETEAMSMEETINGURL") {
            current_event.teams_join_url = Trim(prop_value);
        }
    }

    return events;
}

std::vector<CalendarEvent> IcsParser::GetEventsForDate(
    const std::vector<CalendarEvent>& events,
    const CalendarDate& date) const {

    std::vector<CalendarEvent> matched;
    for (const auto& ev : events) {
        if (ev.occurs_on(date)) {
            matched.push_back(ev.rrule.is_recurring() ? ev.generate_instance_for_date(date) : ev);
        }
    }

    std::sort(matched.begin(), matched.end(), [](const CalendarEvent& a, const CalendarEvent& b) {
        // All day events come first
        if (a.is_all_day != b.is_all_day) {
            return a.is_all_day > b.is_all_day; // true > false
        }

        // Compare start times
        if (a.start_time != b.start_time) {
            return a.start_time < b.start_time;
        }

        // Compare end times
        if (a.end_time != b.end_time) {
            return a.end_time < b.end_time;
        }

        // Compare summary
        if (a.summary != b.summary) {
            return a.summary < b.summary;
        }

        return a.uid < b.uid;
    });

    return matched;
}

int IcsParser::GetLocalTimezoneOffsetMinutes() const {
    return local_tz_offset_minutes_;
}

} // namespace calendar


// --- Source: src/calendar_engine/teams_extractor.cpp ---
#include <algorithm>
#include <cctype>
#include <vector>

namespace calendar {

namespace {

std::string ToLower(std::string_view sv) {
    std::string s;
    s.reserve(sv.size());
    for (char c : sv) {
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return s;
}

std::string UnescapeHtmlEntities(std::string_view sv) {
    std::string result;
    result.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '&') {
            if (sv.substr(i).starts_with("&amp;")) {
                result.push_back('&');
                i += 4;
                continue;
            } else if (sv.substr(i).starts_with("&quot;")) {
                result.push_back('"');
                i += 5;
                continue;
            } else if (sv.substr(i).starts_with("&apos;")) {
                result.push_back('\'');
                i += 5;
                continue;
            } else if (sv.substr(i).starts_with("&#39;")) {
                result.push_back('\'');
                i += 4;
                continue;
            } else if (sv.substr(i).starts_with("&lt;")) {
                result.push_back('<');
                i += 3;
                continue;
            } else if (sv.substr(i).starts_with("&gt;")) {
                result.push_back('>');
                i += 3;
                continue;
            }
        }
        result.push_back(sv[i]);
    }
    return result;
}

// Clean trailing punctuation attached to URLs in plain text sentences
std::string_view TrimTrailingUrlPunctuation(std::string_view url) {
    while (!url.empty()) {
        char back = url.back();
        if (back == '.' || back == ',' || back == ';' || back == ')' || 
            back == ']' || back == '}' || back == '>' || back == '"' || 
            back == '\'' || back == '\\' || back == '\r' || back == '\n') {
            url.remove_suffix(1);
        } else {
            break;
        }
    }
    return url;
}

// Extract host and path from a URL
bool ParseUrlHostAndPath(std::string_view url, std::string& out_host, std::string& out_path) {
    size_t scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos) {
        return false;
    }

    size_t host_start = scheme_pos + 3;
    size_t host_end = url.find_first_of("/:?#", host_start);

    if (host_end == std::string::npos) {
        out_host = ToLower(url.substr(host_start));
        out_path = "/";
    } else {
        out_host = ToLower(url.substr(host_start, host_end - host_start));
        out_path = std::string(url.substr(host_end));
    }

    return true;
}

} // namespace

bool TeamsExtractor::IsTeamsUrl(std::string_view url) {
    std::string host, path;
    if (!ParseUrlHostAndPath(url, host, path)) {
        return false;
    }

    // Check Teams host domains
    bool is_teams_host = false;
    if (host == "teams.microsoft.com" || 
        (host.size() > 18 && host.ends_with(".teams.microsoft.com"))) {
        is_teams_host = true;
    } else if (host == "teams.live.com" || 
               (host.size() > 15 && host.ends_with(".teams.live.com"))) {
        is_teams_host = true;
    } else if (host == "teams.microsoft.us" || 
               (host.size() > 19 && host.ends_with(".teams.microsoft.us"))) {
        is_teams_host = true;
    }

    if (!is_teams_host) {
        return false;
    }

    // Check meeting path indicators
    std::string lower_path = ToLower(path);
    if (lower_path.find("/l/meetup-join/") != std::string::npos ||
        lower_path.find("/meet/") != std::string::npos) {
        return true;
    }

    return false;
}

std::string TeamsExtractor::UrlDecode(std::string_view encoded) {
    std::string result;
    result.reserve(encoded.size());

    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            char h1 = encoded[i + 1];
            char h2 = encoded[i + 2];
            auto from_hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int d1 = from_hex(h1);
            int d2 = from_hex(h2);
            if (d1 >= 0 && d2 >= 0) {
                result.push_back(static_cast<char>((d1 << 4) | d2));
                i += 2;
                continue;
            }
        }
        result.push_back(encoded[i]);
    }

    return result;
}

std::optional<std::string> TeamsExtractor::DecodeSafeLink(std::string_view safelink_url) {
    std::string unescaped = UnescapeHtmlEntities(safelink_url);
    std::string host, path;
    if (!ParseUrlHostAndPath(unescaped, host, path)) {
        return std::nullopt;
    }

    if (host != "safelinks.protection.outlook.com" &&
        !(host.size() > 32 && host.ends_with(".safelinks.protection.outlook.com"))) {
        return std::nullopt;
    }

    // Look for ?url= or &url=
    size_t url_param_pos = unescaped.find("?url=");
    if (url_param_pos == std::string_view::npos) {
        url_param_pos = unescaped.find("&url=");
    }
    if (url_param_pos == std::string_view::npos) {
        return std::nullopt;
    }

    size_t val_start = url_param_pos + 5;
    size_t val_end = unescaped.find_first_of("&#", val_start);
    if (val_end == std::string_view::npos) {
        val_end = unescaped.size();
    }

    std::string_view encoded_target = std::string_view(unescaped).substr(val_start, val_end - val_start);
    std::string decoded = UrlDecode(encoded_target);
    if (!decoded.empty()) {
        return decoded;
    }

    return std::nullopt;
}

std::string TeamsExtractor::ExtractFromEvent(const CalendarEvent& event) {
    if (!event.teams_join_url.empty() && IsTeamsUrl(event.teams_join_url)) {
        return event.teams_join_url;
    }
    if (!event.location.empty()) {
        std::string from_loc = ExtractTeamsUrl(event.location);
        if (!from_loc.empty()) return from_loc;
    }
    if (!event.description.empty()) {
        std::string from_desc = ExtractTeamsUrl(event.description);
        if (!from_desc.empty()) return from_desc;
    }
    if (!event.summary.empty()) {
        std::string from_sum = ExtractTeamsUrl(event.summary);
        if (!from_sum.empty()) return from_sum;
    }
    return "";
}

std::string TeamsExtractor::ExtractTeamsUrl(std::string_view text) {
    // 1. Search for HTML anchor hrefs first: <a ... href="..." ...>
    size_t a_pos = 0;
    while (a_pos < text.size()) {
        size_t tag_start = text.find("<a", a_pos);
        if (tag_start == std::string_view::npos) {
            tag_start = text.find("<A", a_pos);
        }
        if (tag_start == std::string_view::npos) {
            break;
        }

        // Must be followed by whitespace
        if (tag_start + 2 < text.size() && !std::isspace(static_cast<unsigned char>(text[tag_start + 2]))) {
            a_pos = tag_start + 2;
            continue;
        }

        size_t tag_end = text.find('>', tag_start);
        if (tag_end == std::string_view::npos) {
            break;
        }

        std::string_view tag_content = text.substr(tag_start, tag_end - tag_start);
        // Find href=
        std::string lower_tag = ToLower(tag_content);
        size_t href_pos = lower_tag.find("href=");
        if (href_pos != std::string::npos) {
            size_t val_start = href_pos + 5;
            while (val_start < tag_content.size() && std::isspace(static_cast<unsigned char>(tag_content[val_start]))) {
                val_start++;
            }
            if (val_start < tag_content.size()) {
                char quote = tag_content[val_start];
                std::string raw_url;
                if (quote == '"' || quote == '\'') {
                    val_start++;
                    size_t quote_end = tag_content.find(quote, val_start);
                    if (quote_end != std::string_view::npos) {
                        raw_url = std::string(tag_content.substr(val_start, quote_end - val_start));
                    }
                } else {
                    size_t val_end = tag_content.find_first_of(" >", val_start);
                    raw_url = std::string(tag_content.substr(val_start, val_end - val_start));
                }

                if (!raw_url.empty()) {
                    std::string unescaped_url = UnescapeHtmlEntities(raw_url);
                    if (IsTeamsUrl(unescaped_url)) {
                        return unescaped_url;
                    }
                    auto decoded_sl = DecodeSafeLink(unescaped_url);
                    if (decoded_sl.has_value() && IsTeamsUrl(*decoded_sl)) {
                        return *decoded_sl;
                    }
                }
            }
        }

        a_pos = tag_end + 1;
    }

    // 2. Fallback to scanning URLs in arbitrary plain or mixed text
    size_t search_pos = 0;
    while (search_pos < text.size()) {
        size_t http_pos = text.find("http://", search_pos);
        size_t https_pos = text.find("https://", search_pos);
        size_t start_pos = std::string_view::npos;

        if (http_pos != std::string_view::npos && https_pos != std::string_view::npos) {
            start_pos = std::min(http_pos, https_pos);
        } else if (http_pos != std::string_view::npos) {
            start_pos = http_pos;
        } else if (https_pos != std::string_view::npos) {
            start_pos = https_pos;
        } else {
            break;
        }

        // Find end of URL (whitespace, quotes, angle brackets)
        size_t end_pos = text.find_first_of(" \t\r\n\"'<>", start_pos);
        if (end_pos == std::string_view::npos) {
            end_pos = text.size();
        }

        std::string_view raw_candidate = text.substr(start_pos, end_pos - start_pos);
        std::string_view cleaned_candidate = TrimTrailingUrlPunctuation(raw_candidate);
        std::string unescaped_candidate = UnescapeHtmlEntities(cleaned_candidate);

        // Check if direct Teams URL
        if (IsTeamsUrl(unescaped_candidate)) {
            return unescaped_candidate;
        }

        // Check if Outlook SafeLink pointing to Teams
        auto decoded_opt = DecodeSafeLink(unescaped_candidate);
        if (decoded_opt.has_value() && IsTeamsUrl(*decoded_opt)) {
            return *decoded_opt;
        }

        search_pos = end_pos;
    }

    return "";
}

} // namespace calendar


// --- Source: src/mod/mod_settings.cpp ---
#include <windows.h>
#include <algorithm>
#include <cctype>

#ifdef WH_MOD
#endif

namespace calendar {

namespace {

std::wstring TrimWhitespace(std::wstring_view sv) {
    size_t first = 0;
    while (first < sv.size() && (sv[first] == L' ' || sv[first] == L'\t' || sv[first] == L'\r' || sv[first] == L'\n')) {
        ++first;
    }
    if (first == sv.size()) {
        return L"";
    }
    size_t last = sv.size() - 1;
    while (last > first && (sv[last] == L' ' || sv[last] == L'\t' || sv[last] == L'\r' || sv[last] == L'\n')) {
        --last;
    }
    return std::wstring(sv.substr(first, last - first + 1));
}

std::wstring Utf8ToUtf16(std::string_view utf8) {
    if (utf8.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size_needed <= 0) return L"";
    std::wstring result(size_needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), size_needed);
    return result;
}

std::string Utf16ToUtf8(std::wstring_view utf16) {
    if (utf16.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) return "";
    std::string result(size_needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), result.data(), size_needed, nullptr, nullptr);
    return result;
}

} // namespace

void ModSettings::Normalize() {
    feed_url = TrimWhitespace(feed_url);
    if (refresh_interval_minutes < kMinimumRefreshIntervalMinutes) {
        refresh_interval_minutes = kMinimumRefreshIntervalMinutes;
    }
}

bool ModSettings::IsFeedUrlConfigured() const {
    return !feed_url.empty();
}

std::string ModSettings::GetFeedUrlUtf8() const {
    return Utf16ToUtf8(feed_url);
}

ModSettings ModSettings::FromValues(
    std::wstring_view url,
    int refresh_minutes,
    bool show_past,
    bool enable_join
) {
    ModSettings settings;
    settings.feed_url = std::wstring(url);
    settings.refresh_interval_minutes = refresh_minutes;
    settings.show_past_events = show_past;
    settings.enable_join_button = enable_join;
    settings.Normalize();
    return settings;
}

ModSettings ModSettings::FromValuesUtf8(
    std::string_view url,
    int refresh_minutes,
    bool show_past,
    bool enable_join
) {
    return FromValues(Utf8ToUtf16(url), refresh_minutes, show_past, enable_join);
}

void ModSettings::LoadFromWindhawk() {
#ifdef WH_MOD
    PCWSTR url_setting = Wh_GetStringSetting(L"feed_url");
    if (url_setting) {
        feed_url = url_setting;
        Wh_FreeStringSetting(url_setting);
    } else {
        feed_url.clear();
    }

    refresh_interval_minutes = Wh_GetIntSetting(L"refresh_interval_minutes");
    if (refresh_interval_minutes <= 0) {
        refresh_interval_minutes = kDefaultRefreshIntervalMinutes;
    }

    show_past_events = Wh_GetIntSetting(L"show_past_events") != 0;
    enable_join_button = Wh_GetIntSetting(L"enable_join_button") != 0;

    Normalize();
#else
    Normalize();
#endif
}

} // namespace calendar


// --- Source: src/mod/calendar_cache.cpp ---
#include <algorithm>

namespace calendar {

std::string_view to_string(FetchStatus status) {
    switch (status) {
        case FetchStatus::Idle: return "Idle";
        case FetchStatus::Fetching: return "Fetching";
        case FetchStatus::Succeeded: return "Succeeded";
        case FetchStatus::Failed: return "Failed";
    }
    return "Unknown";
}

void CalendarCache::SetFetchStatus(FetchStatus status, std::string_view error_message) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    status_ = status;
    if (status == FetchStatus::Failed) {
        last_error_ = std::string(error_message);
    } else if (status == FetchStatus::Succeeded) {
        last_error_.clear();
    }
}

void CalendarCache::UpdateEvents(std::vector<CalendarEvent> new_events) {
    // Sort chronologically by start timestamp before caching
    std::sort(new_events.begin(), new_events.end(), [](const CalendarEvent& a, const CalendarEvent& b) {
        if (a.start_time != b.start_time) {
            return a.start_time < b.start_time;
        }
        return a.end_time < b.end_time;
    });

    UpdateCallback cb;
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        events_ = std::move(new_events);
        status_ = FetchStatus::Succeeded;
        last_error_.clear();
        last_updated_time_ = std::chrono::system_clock::now();
        cb = on_updated_cb_;
    }

    if (cb) {
        cb();
    }
}

std::vector<CalendarEvent> CalendarCache::GetEventsSnapshot() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return events_;
}

std::vector<CalendarEvent> CalendarCache::GetEventsForDate(const CalendarDate& target_date, bool show_past_events) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<CalendarEvent> result;
    result.reserve(events_.size());

    for (const auto& ev : events_) {
        if (!ev.occurs_on(target_date)) {
            continue;
        }
        CalendarEvent instance = ev.rrule.is_recurring() ? ev.generate_instance_for_date(target_date) : ev;
        if (!show_past_events && instance.temporal_status == MeetingTemporalStatus::Past) {
            continue;
        }
        result.push_back(std::move(instance));
    }

    return result;
}

FetchStatus CalendarCache::GetStatus() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return status_;
}

std::string CalendarCache::GetLastError() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return last_error_;
}

std::chrono::system_clock::time_point CalendarCache::GetLastUpdatedTime() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return last_updated_time_;
}

size_t CalendarCache::EventCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return events_.size();
}

bool CalendarCache::HasEvents() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return !events_.empty();
}

void CalendarCache::Clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    events_.clear();
    status_ = FetchStatus::Idle;
    last_error_.clear();
    last_updated_time_ = {};
}

void CalendarCache::SetOnUpdatedCallback(UpdateCallback cb) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    on_updated_cb_ = std::move(cb);
}

} // namespace calendar


// --- Source: src/mod/http_fetcher.cpp ---
#include <windows.h>
#include <wininet.h>
#include <ctime>
#include <algorithm>

namespace calendar {

// --- WinINetHttpTransport Implementation ---

WinINetHttpTransport::WinINetHttpTransport() = default;

WinINetHttpTransport::~WinINetHttpTransport() = default;

HttpResponse WinINetHttpTransport::Fetch(const std::wstring& url) {
    if (url.empty()) {
        return HttpResponse{0, "", "Empty URL specified"};
    }

    HINTERNET hInternet = InternetOpenW(
        L"TeamsCalendarFlyout/1.0",
        INTERNET_OPEN_TYPE_PRECONFIG,
        nullptr,
        nullptr,
        0
    );
    if (!hInternet) {
        DWORD err = GetLastError();
        return HttpResponse{0, "", "InternetOpenW failed with code " + std::to_string(err)};
    }

    DWORD timeout_connect_ms = 10000;
    DWORD timeout_receive_ms = 15000;
    InternetSetOptionW(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout_connect_ms, sizeof(timeout_connect_ms));
    InternetSetOptionW(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout_receive_ms, sizeof(timeout_receive_ms));

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE;
    if (url.rfind(L"https://", 0) == 0 || url.rfind(L"HTTPS://", 0) == 0) {
        flags |= INTERNET_FLAG_SECURE;
    }

    HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), nullptr, 0, flags, 0);
    if (!hUrl) {
        DWORD err = GetLastError();
        InternetCloseHandle(hInternet);
        return HttpResponse{0, "", "InternetOpenUrlW failed with code " + std::to_string(err)};
    }

    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    DWORD index = 0;
    if (!HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status_code, &status_code_size, &index)) {
        status_code = 200; // Fallback if server / source omitted HTTP response header
    }

    std::string body;
    char buffer[8192];
    DWORD bytes_read = 0;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytes_read) && bytes_read > 0) {
        body.append(buffer, bytes_read);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    if (status_code < 200 || status_code >= 300) {
        return HttpResponse{
            static_cast<int>(status_code),
            body,
            "HTTP error " + std::to_string(status_code)
        };
    }

    return HttpResponse{static_cast<int>(status_code), body, ""};
}

// --- MockHttpTransport Implementation ---

void MockHttpTransport::SetResponse(int status_code, std::string body, std::string error_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_code_ = status_code;
    body_ = std::move(body);
    error_message_ = std::move(error_message);
}

HttpResponse MockHttpTransport::Fetch(const std::wstring& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    fetch_count_++;
    last_requested_url_ = url;
    return HttpResponse{status_code_, body_, error_message_};
}

int MockHttpTransport::GetFetchCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fetch_count_;
}

std::wstring MockHttpTransport::GetLastRequestedUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_requested_url_;
}

// --- HttpFetcher Implementation ---

HttpFetcher::HttpFetcher(
    std::shared_ptr<CalendarCache> cache,
    std::shared_ptr<IHttpTransport> transport
) : cache_(std::move(cache)), transport_(std::move(transport)) {
    if (!transport_) {
        transport_ = std::make_shared<WinINetHttpTransport>();
    }
}

HttpFetcher::~HttpFetcher() {
    Stop();
}

void HttpFetcher::Start(const ModSettings& settings) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (is_running_) return;
        stop_requested_ = false;
        refresh_requested_ = false;
        current_settings_ = settings;
        is_running_ = true;
    }
    worker_thread_ = std::thread(&HttpFetcher::WorkerLoop, this);
}

void HttpFetcher::Stop() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!is_running_) return;
        stop_requested_ = true;
        is_running_ = false;
        cv_.notify_all();
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void HttpFetcher::UpdateSettings(const ModSettings& settings) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool url_changed = (current_settings_.feed_url != settings.feed_url);
    current_settings_ = settings;
    if (url_changed) {
        refresh_requested_ = true;
        cv_.notify_all();
    }
}

void HttpFetcher::TriggerFetch() {
    std::unique_lock<std::mutex> lock(mutex_);
    refresh_requested_ = true;
    cv_.notify_all();
}

bool HttpFetcher::FetchOnce(const std::wstring& url) {
    if (url.empty()) {
        cache_->SetFetchStatus(FetchStatus::Idle);
        return false;
    }

    cache_->SetFetchStatus(FetchStatus::Fetching);
    auto response = transport_->Fetch(url);

    if (!response.is_success()) {
        std::string err = response.error_message.empty()
            ? ("HTTP " + std::to_string(response.status_code))
            : response.error_message;
        cache_->SetFetchStatus(FetchStatus::Failed, err);
        return false;
    }

    try {
        IcsParser parser;
        auto events = parser.Parse(response.body);

        // Enrich events with Teams meeting links and temporal status relative to system clock
        auto now_unix = static_cast<int64_t>(std::time(nullptr));
        auto ref_now = CalendarDateTime::from_unix_timestamp(now_unix);
        MeetingStatusCalculator::EnrichEvents(events, ref_now);

        cache_->UpdateEvents(std::move(events));
        return true;
    } catch (const std::exception& e) {
        cache_->SetFetchStatus(FetchStatus::Failed, std::string("Parser error: ") + e.what());
        return false;
    }
}

bool HttpFetcher::IsRunning() const {
    return is_running_;
}

void HttpFetcher::WorkerLoop() {
    while (true) {
        std::wstring url_to_fetch;
        int interval_minutes = 15;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stop_requested_) break;
            url_to_fetch = current_settings_.feed_url;
            interval_minutes = current_settings_.refresh_interval_minutes;
            refresh_requested_ = false;
        }

        if (!url_to_fetch.empty()) {
            FetchOnce(url_to_fetch);
        }

        // Wait for next scheduled refresh or explicit trigger/stop
        std::unique_lock<std::mutex> lock(mutex_);
        auto wait_duration = std::chrono::minutes(std::max(1, interval_minutes));
        cv_.wait_for(lock, wait_duration, [this] {
            return stop_requested_ || refresh_requested_;
        });

        if (stop_requested_) break;
    }
}

} // namespace calendar


// --- Source: src/ui/agenda_view_model.cpp ---
#include <cstdio>
#include <algorithm>
#include <array>

namespace calendar {

namespace {

int GetDayOfWeek(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static const char* kDayNames[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

static const char* kMonthNames[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

} // namespace

ThemePalette ThemePalette::ForTheme(ThemeMode mode) {
    ThemePalette p;
    switch (mode) {
    case ThemeMode::Light:
        p.card_background = "#FFFFFF";
        p.card_border = "#E5E5E5";
        p.primary_text = "#1A1A1A";
        p.secondary_text = "#5C5C5C";
        p.teams_accent = "#464EB8";
        p.teams_accent_hover = "#5B63D3";
        p.teams_accent_pressed = "#3B429F";
        p.live_badge_bg = "#107C41";
        p.live_badge_fg = "#FFFFFF";
        p.soon_badge_bg = "#D83B01";
        p.soon_badge_fg = "#FFFFFF";
        p.upcoming_badge_bg = "#0078D4";
        p.upcoming_badge_fg = "#FFFFFF";
        p.past_badge_bg = "#797775";
        p.past_badge_fg = "#FFFFFF";
        p.empty_text = "#616161";
        p.header_text = "#242424";
        break;

    case ThemeMode::HighContrast:
        p.card_background = "#000000";
        p.card_border = "#FFFFFF";
        p.primary_text = "#FFFFFF";
        p.secondary_text = "#FFFF00";
        p.teams_accent = "#00FFFF";
        p.teams_accent_hover = "#33FFFF";
        p.teams_accent_pressed = "#00CCCC";
        p.live_badge_bg = "#008000";
        p.live_badge_fg = "#FFFFFF";
        p.soon_badge_bg = "#FFA500";
        p.soon_badge_fg = "#000000";
        p.upcoming_badge_bg = "#00FFFF";
        p.upcoming_badge_fg = "#000000";
        p.past_badge_bg = "#808080";
        p.past_badge_fg = "#FFFFFF";
        p.empty_text = "#FFFFFF";
        p.header_text = "#FFFFFF";
        break;

    case ThemeMode::Dark:
    default:
        p.card_background = "#202020";
        p.card_border = "#383838";
        p.primary_text = "#FFFFFF";
        p.secondary_text = "#CCCCCC";
        p.teams_accent = "#6264A7";
        p.teams_accent_hover = "#7B7DC4";
        p.teams_accent_pressed = "#51538C";
        p.live_badge_bg = "#107C41";
        p.live_badge_fg = "#FFFFFF";
        p.soon_badge_bg = "#D83B01";
        p.soon_badge_fg = "#FFFFFF";
        p.upcoming_badge_bg = "#0078D4";
        p.upcoming_badge_fg = "#FFFFFF";
        p.past_badge_bg = "#797775";
        p.past_badge_fg = "#E0E0E0";
        p.empty_text = "#9E9E9E";
        p.header_text = "#E0E0E0";
        break;
    }
    return p;
}

std::string AgendaPresenter::FormatTimeRange(const CalendarDateTime& start, const CalendarDateTime& end, bool is_all_day) {
    if (is_all_day) {
        return "All Day";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d - %02d:%02d",
                  start.time.hour, start.time.minute,
                  end.time.hour, end.time.minute);
    return std::string(buf);
}

std::string AgendaPresenter::FormatDateHeader(const CalendarDate& date) {
    if (date.year <= 0 || date.month < 1 || date.month > 12 || date.day < 1 || date.day > 31) {
        return date.to_string();
    }
    int dow = GetDayOfWeek(date.year, date.month, date.day);
    const char* day_name = (dow >= 0 && dow < 7) ? kDayNames[dow] : "";
    const char* month_name = (date.month >= 1 && date.month <= 12) ? kMonthNames[date.month - 1] : "";

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s, %s %d, %04d", day_name, month_name, date.day, date.year);
    return std::string(buf);
}

AgendaItemViewModel AgendaItemViewModel::FromCalendarEvent(
    const CalendarEvent& event,
    ThemeMode theme,
    const CalendarDateTime& reference_time,
    bool enable_join
) {
    AgendaItemViewModel vm;
    vm.uid = event.uid;
    vm.title = event.summary;
    vm.location = event.location;
    vm.is_teams = event.is_teams_meeting;
    vm.join_url = event.teams_join_url;
    vm.enable_join_button = enable_join;
    vm.is_all_day = event.is_all_day;
    vm.start_unix = event.start_time.to_unix_timestamp();
    vm.end_unix = event.end_time.to_unix_timestamp();

    // Recompute or verify temporal status relative to reference_time
    vm.temporal_status = MeetingStatusCalculator::CalculateStatus(event, reference_time);

    vm.palette = ThemePalette::ForTheme(theme);

    vm.formatted_time_range = AgendaPresenter::FormatTimeRange(event.start_time, event.end_time, event.is_all_day);

    switch (vm.temporal_status) {
    case MeetingTemporalStatus::Live:
        vm.status_badge_label = "Live Now";
        vm.status_badge_bg = vm.palette.live_badge_bg;
        vm.status_badge_fg = vm.palette.live_badge_fg;
        break;
    case MeetingTemporalStatus::Soon:
        vm.status_badge_label = "Starting Soon";
        vm.status_badge_bg = vm.palette.soon_badge_bg;
        vm.status_badge_fg = vm.palette.soon_badge_fg;
        break;
    case MeetingTemporalStatus::Past:
        vm.status_badge_label = "Past";
        vm.status_badge_bg = vm.palette.past_badge_bg;
        vm.status_badge_fg = vm.palette.past_badge_fg;
        break;
    case MeetingTemporalStatus::Future:
    default:
        vm.status_badge_label = "Upcoming";
        vm.status_badge_bg = vm.palette.upcoming_badge_bg;
        vm.status_badge_fg = vm.palette.upcoming_badge_fg;
        break;
    }

    return vm;
}

size_t AgendaDayViewModel::teams_meetings_count() const {
    size_t count = 0;
    for (const auto& item : items) {
        if (item.is_teams) {
            ++count;
        }
    }
    return count;
}

AgendaDayViewModel AgendaPresenter::PresentEvents(
    const std::vector<CalendarEvent>& events,
    const CalendarDate& date,
    ThemeMode theme,
    const CalendarDateTime& reference_time,
    bool show_past_events,
    bool enable_join_button
) {
    AgendaDayViewModel day_vm;
    day_vm.selected_date = date;
    day_vm.formatted_date_header = FormatDateHeader(date);
    day_vm.theme = theme;

    std::vector<AgendaItemViewModel> vms;
    for (const auto& ev : events) {
        if (!ev.occurs_on(date)) {
            continue;
        }

        CalendarEvent instance = ev.rrule.is_recurring() ? ev.generate_instance_for_date(date) : ev;

        AgendaItemViewModel vm = AgendaItemViewModel::FromCalendarEvent(
            instance, theme, reference_time, enable_join_button
        );

        if (!show_past_events && vm.temporal_status == MeetingTemporalStatus::Past) {
            continue;
        }

        vms.push_back(std::move(vm));
    }

    // Chronological sorting:
    // 1. All-day events first
    // 2. Timed events sorted by start_unix
    // 3. Ties broken by title
    std::sort(vms.begin(), vms.end(), [](const AgendaItemViewModel& a, const AgendaItemViewModel& b) {
        if (a.is_all_day != b.is_all_day) {
            return a.is_all_day > b.is_all_day; // all-day comes first
        }
        if (a.start_unix != b.start_unix) {
            return a.start_unix < b.start_unix;
        }
        return a.title < b.title;
    });

    day_vm.items = std::move(vms);
    return day_vm;
}

AgendaDayViewModel AgendaPresenter::PresentDay(
    const CalendarCache& cache,
    const CalendarDate& date,
    ThemeMode theme,
    const CalendarDateTime& reference_time,
    bool show_past_events,
    bool enable_join_button
) {
    auto events = cache.GetEventsForDate(date, /*show_past_events=*/true);
    return PresentEvents(events, date, theme, reference_time, show_past_events, enable_join_button);
}

} // namespace calendar


// --- Source: src/ui/xaml_injector.cpp ---
#include <windows.h>
#include <shellapi.h>
#include <sstream>
#include <ctime>
#include <cctype>

namespace calendar {

bool UrlSanitizer::IsSafeMeetingUrl(std::string_view url) {
    std::string sanitized;
    return SanitizeAndValidateUrl(url, &sanitized);
}

bool UrlSanitizer::SanitizeAndValidateUrl(std::string_view url, std::string* sanitized_out) {
    // 1. Trim leading and trailing whitespace
    size_t start = 0;
    while (start < url.size() && std::isspace(static_cast<unsigned char>(url[start]))) {
        start++;
    }
    size_t end = url.size();
    while (end > start && std::isspace(static_cast<unsigned char>(url[end - 1]))) {
        end--;
    }

    if (start >= end) {
        return false;
    }

    std::string_view trimmed = url.substr(start, end - start);

    // 2. Reject control characters (\r, \n, \0, tabs, etc.)
    for (char c : trimmed) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32 || uc == 127) {
            return false;
        }
    }

    // 3. Find scheme separator ':'
    size_t colon_pos = trimmed.find(':');
    if (colon_pos == std::string_view::npos || colon_pos == 0) {
        return false;
    }

    std::string scheme;
    scheme.reserve(colon_pos);
    for (size_t i = 0; i < colon_pos; ++i) {
        char c = trimmed[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '-' && c != '.') {
            return false;
        }
        scheme.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    // 4. Validate allowed schemes: http, https, msteams
    if (scheme == "https" || scheme == "http") {
        if (!trimmed.substr(colon_pos).starts_with("://")) {
            return false;
        }
        // Ensure host is non-empty
        size_t host_start = colon_pos + 3;
        if (host_start >= trimmed.size()) {
            return false;
        }
        size_t host_end = trimmed.find_first_of("/?#", host_start);
        std::string_view host = (host_end == std::string_view::npos)
            ? trimmed.substr(host_start)
            : trimmed.substr(host_start, host_end - host_start);
        if (host.empty()) {
            return false;
        }
    } else if (scheme == "msteams") {
        // e.g. msteams:/l/meetup-join/... or msteams://...
        size_t rest_start = colon_pos + 1;
        while (rest_start < trimmed.size() && trimmed[rest_start] == '/') {
            rest_start++;
        }
        if (rest_start >= trimmed.size()) {
            return false;
        }
    } else {
        // Reject all other schemes: file, javascript, data, cmd, powershell, etc.
        return false;
    }

    if (sanitized_out) {
        *sanitized_out = std::string(trimmed);
    }
    return true;
}

std::string XamlMarkupGenerator::EscapeXml(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 11 / 10);
    for (char c : text) {
        switch (c) {
        case '&':  out.append("&amp;"); break;
        case '<':  out.append("&lt;"); break;
        case '>':  out.append("&gt;"); break;
        case '"':  out.append("&quot;"); break;
        case '\'': out.append("&apos;"); break;
        default:   out.push_back(c); break;
        }
    }
    return out;
}

std::string XamlMarkupGenerator::GenerateAgendaXaml(const AgendaDayViewModel& viewModel, int maxHeight) {
    ThemePalette palette = ThemePalette::ForTheme(viewModel.theme);
    std::ostringstream ss;

    ss << "<ScrollViewer x:Name=\"TeamsAgendaScrollViewer\" MaxHeight=\"" << maxHeight
       << "\" VerticalScrollBarVisibility=\"Auto\" HorizontalScrollBarVisibility=\"Disabled\" "
       << "Padding=\"4,4,4,4\" Margin=\"0,8,0,0\">\n";
    ss << "  <StackPanel x:Name=\"TeamsAgendaRootStackPanel\" Spacing=\"6\" HorizontalAlignment=\"Stretch\">\n";

    // Date header
    ss << "    <TextBlock x:Name=\"TeamsAgendaDateHeader\" Text=\""
       << EscapeXml(viewModel.formatted_date_header)
       << "\" FontSize=\"12\" FontWeight=\"SemiBold\" Foreground=\""
       << palette.header_text << "\" Margin=\"4,2,4,4\"/>\n";

    if (viewModel.is_empty()) {
        ss << "    <Border x:Name=\"TeamsAgendaEmptyBorder\" Padding=\"16\" HorizontalAlignment=\"Center\" VerticalAlignment=\"Center\">\n";
        ss << "      <TextBlock x:Name=\"TeamsAgendaEmptyText\" Text=\""
           << EscapeXml(viewModel.empty_state_message)
           << "\" Foreground=\"" << palette.empty_text
           << "\" FontStyle=\"Italic\" TextAlignment=\"Center\"/>\n";
        ss << "    </Border>\n";
    } else {
        for (size_t i = 0; i < viewModel.items.size(); ++i) {
            const auto& item = viewModel.items[i];
            ss << "    <Border x:Name=\"MeetingCard_" << i << "\" Background=\"" << item.card_bg()
               << "\" BorderBrush=\"" << item.card_border()
               << "\" BorderThickness=\"1\" CornerRadius=\"6\" Padding=\"10,8,10,8\" Margin=\"0,2,0,2\">\n";
            ss << "      <StackPanel Spacing=\"4\">\n";

            // Time and Status badge row
            ss << "        <Grid ColumnDefinitions=\"Auto,*,Auto\">\n";
            ss << "          <TextBlock Grid.Column=\"0\" Text=\""
               << EscapeXml(item.formatted_time_range)
               << "\" FontSize=\"12\" FontWeight=\"SemiBold\" Foreground=\""
               << item.secondary_text_color() << "\"/>\n";
            ss << "          <StackPanel Grid.Column=\"2\" Orientation=\"Horizontal\" Spacing=\"6\" HorizontalAlignment=\"Right\">\n";
            ss << "            <Border Background=\"" << item.status_badge_bg
               << "\" CornerRadius=\"3\" Padding=\"5,1,5,1\">\n";
            ss << "              <TextBlock Text=\"" << EscapeXml(item.status_badge_label)
               << "\" FontSize=\"10\" FontWeight=\"Bold\" Foreground=\""
               << item.status_badge_fg << "\"/>\n";
            ss << "            </Border>\n";

            if (item.is_teams) {
                ss << "            <Border Background=\"" << item.accent_color()
                   << "\" CornerRadius=\"3\" Padding=\"4,1,4,1\">\n";
                ss << "              <TextBlock Text=\"Teams\" FontSize=\"10\" FontWeight=\"Bold\" Foreground=\"#FFFFFF\"/>\n";
                ss << "            </Border>\n";
            }
            ss << "          </StackPanel>\n";
            ss << "        </Grid>\n";

            // Title
            ss << "        <TextBlock Text=\"" << EscapeXml(item.title)
               << "\" FontSize=\"13\" FontWeight=\"Bold\" TextWrapping=\"Wrap\" Foreground=\""
               << item.primary_text_color() << "\"/>\n";

            // Location if non-empty
            if (!item.location.empty()) {
                ss << "        <TextBlock Text=\"" << EscapeXml(item.location)
                   << "\" FontSize=\"11\" Foreground=\""
                   << item.secondary_text_color() << "\" TextTrimming=\"CharacterEllipsis\"/>\n";
            }

            // Teams Join Button (Fluent Design styled with rounded corners, icon, and hover-ready styling)
            if (item.is_teams && item.enable_join_button && !item.join_url.empty()) {
                ss << "        <Button x:Name=\"JoinButton_" << i << "\" Tag=\""
                   << EscapeXml(item.join_url)
                   << "\" Background=\"" << item.accent_color()
                   << "\" Foreground=\"#FFFFFF\" BorderThickness=\"1\" BorderBrush=\"#00000000\" "
                   << "CornerRadius=\"4\" Padding=\"10,4,10,4\" HorizontalAlignment=\"Left\" Margin=\"0,4,0,0\">\n";
                ss << "          <StackPanel Orientation=\"Horizontal\" Spacing=\"6\" VerticalAlignment=\"Center\">\n";
                ss << "            <TextBlock Text=\"&#xE714;\" FontFamily=\"Segoe Fluent Icons\" FontSize=\"12\" VerticalAlignment=\"Center\"/>\n";
                ss << "            <TextBlock Text=\"Join Meeting\" FontWeight=\"SemiBold\" FontSize=\"12\" VerticalAlignment=\"Center\"/>\n";
                ss << "          </StackPanel>\n";
                ss << "        </Button>\n";
            }

            ss << "      </StackPanel>\n";
            ss << "    </Border>\n";
        }
    }

    ss << "  </StackPanel>\n";
    ss << "</ScrollViewer>";
    return ss.str();
}

bool WinUiXamlHostAdapter::FindTargetContainer(std::string_view container_name) {
    return !container_name.empty();
}

bool WinUiXamlHostAdapter::InjectElement(std::string_view container_name, std::string_view element_id, std::string_view xaml_content) {
    return !container_name.empty() && !element_id.empty() && !xaml_content.empty();
}

bool WinUiXamlHostAdapter::RemoveElement(std::string_view container_name, std::string_view element_id) {
    return !container_name.empty() && !element_id.empty();
}

bool WinUiXamlHostAdapter::UpdateElement(std::string_view element_id, std::string_view xaml_content) {
    return !element_id.empty() && !xaml_content.empty();
}

ThemeMode WinUiXamlHostAdapter::DetectThemeMode() const {
    HIGHCONTRASTW hc = { sizeof(hc) };
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON)) {
        return ThemeMode::HighContrast;
    }

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD appsUseLightTheme = 0;
        DWORD dataSize = sizeof(appsUseLightTheme);
        DWORD type = 0;
        LONG res = RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, &type,
                                    reinterpret_cast<LPBYTE>(&appsUseLightTheme), &dataSize);
        RegCloseKey(hKey);

        if (res == ERROR_SUCCESS && appsUseLightTheme == 1) {
            return ThemeMode::Light;
        }
    }

    return ThemeMode::Dark;
}

bool WinUiXamlHostAdapter::OpenMeetingUrl(std::string_view url) {
    std::string sanitized;
    if (!UrlSanitizer::SanitizeAndValidateUrl(url, &sanitized)) {
        return false;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, sanitized.data(), static_cast<int>(sanitized.size()), nullptr, 0);
    if (wlen <= 0) {
        return false;
    }
    std::wstring wurl(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, sanitized.data(), static_cast<int>(sanitized.size()), wurl.data(), wlen);

    HINSTANCE hInst = ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(hInst) > 32;
}

XamlInjector::XamlInjector() = default;

XamlInjector::~XamlInjector() {
    Shutdown();
}

bool XamlInjector::Initialize(
    std::shared_ptr<CalendarCache> cache,
    const ModSettings& settings,
    std::shared_ptr<IXamlHostAdapter> adapter
) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_ = std::move(cache);
    settings_ = settings;
    adapter_ = std::move(adapter);
    if (!adapter_) {
        adapter_ = std::make_shared<WinUiXamlHostAdapter>();
    }

    current_theme_ = adapter_->DetectThemeMode();

    // Default selected date to today (local system date)
    std::time_t t = std::time(nullptr);
    std::tm tm_local{};
#if defined(_WIN32)
    localtime_s(&tm_local, &t);
#else
    localtime_r(&t, &tm_local);
#endif
    selected_date_ = CalendarDate{tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday};

    RenderAndInjectLocked(std::nullopt);
    return true;
}

void XamlInjector::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_injected_ && adapter_) {
        adapter_->RemoveElement(kDefaultTargetContainer, kAgendaPanelElementId);
        is_injected_ = false;
    }
    current_xaml_.clear();
    current_view_model_ = AgendaDayViewModel();
}

void XamlInjector::OnDateSelected(const CalendarDate& new_date, std::optional<CalendarDateTime> reference_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    selected_date_ = new_date;
    RenderAndInjectLocked(reference_time);
}

void XamlInjector::OnThemeChanged(ThemeMode new_theme) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_theme_ = new_theme;
    RenderAndInjectLocked(std::nullopt);
}

void XamlInjector::OnSettingsChanged(const ModSettings& new_settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = new_settings;
    RenderAndInjectLocked(std::nullopt);
}

void XamlInjector::OnEventsUpdated(std::optional<CalendarDateTime> reference_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    RenderAndInjectLocked(reference_time);
}

bool XamlInjector::HandleJoinButtonClick(std::string_view join_url) {
    if (!UrlSanitizer::IsSafeMeetingUrl(join_url)) {
        return false;
    }

    std::shared_ptr<IXamlHostAdapter> adapter;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        adapter = adapter_;
    }
    if (adapter) {
        return adapter->OpenMeetingUrl(join_url);
    }
    return false;
}

const CalendarDate& XamlInjector::GetSelectedDate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return selected_date_;
}

ThemeMode XamlInjector::GetCurrentTheme() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_theme_;
}

bool XamlInjector::IsInjected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return is_injected_;
}

std::string XamlInjector::GetCurrentXaml() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_xaml_;
}

AgendaDayViewModel XamlInjector::GetCurrentViewModel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_view_model_;
}

void XamlInjector::RenderAndInjectLocked(std::optional<CalendarDateTime> reference_time) {
    CalendarDateTime ref_time = reference_time.has_value() ? *reference_time : CalendarDateTime::now_local();

    if (cache_) {
        current_view_model_ = AgendaPresenter::PresentDay(
            *cache_,
            selected_date_,
            current_theme_,
            ref_time,
            settings_.show_past_events,
            settings_.enable_join_button
        );
    } else {
        current_view_model_ = AgendaDayViewModel();
        current_view_model_.selected_date = selected_date_;
        current_view_model_.formatted_date_header = AgendaPresenter::FormatDateHeader(selected_date_);
        current_view_model_.theme = current_theme_;
    }

    current_xaml_ = XamlMarkupGenerator::GenerateAgendaXaml(current_view_model_, kDefaultMaxHeight);

    if (adapter_) {
        if (!is_injected_) {
            if (adapter_->InjectElement(kDefaultTargetContainer, kAgendaPanelElementId, current_xaml_)) {
                is_injected_ = true;
            }
        } else {
            adapter_->UpdateElement(kAgendaPanelElementId, current_xaml_);
        }
    }
}

} // namespace calendar


// --- Source: src/mod/teams_calendar_flyout.wh.cpp ---



#include <windows.h>
#ifdef WH_MOD
#ifndef WH_MOD_ID
#define WH_MOD_ID L"teams-calendar-flyout"
#endif
#else
#ifndef Wh_Log
#define Wh_Log(...) ((void)0)
#endif
#endif

namespace calendar {

ModLifecycleManager::ModLifecycleManager()
    : cache_(std::make_shared<CalendarCache>()),
      injector_(std::make_shared<XamlInjector>()) {}

ModLifecycleManager::~ModLifecycleManager() {
    Uninitialize();
}

ModLifecycleManager& ModLifecycleManager::Instance() {
    static ModLifecycleManager instance;
    return instance;
}

bool ModLifecycleManager::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return true;
    }

    settings_.LoadFromWindhawk();

    if (!injector_) {
        injector_ = std::make_shared<XamlInjector>();
    }
    injector_->Initialize(cache_, settings_, custom_adapter_);

    cache_->SetOnUpdatedCallback([this]() {
        std::shared_ptr<XamlInjector> inj;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            inj = injector_;
        }
        if (inj) {
            inj->OnEventsUpdated();
        }
    });

    fetcher_ = std::make_shared<HttpFetcher>(cache_, custom_transport_);
    fetcher_->Start(settings_);

    initialized_ = true;
    return true;
}

void ModLifecycleManager::OnSettingsChanged() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return;
    }

    settings_.LoadFromWindhawk();
    if (fetcher_) {
        fetcher_->UpdateSettings(settings_);
    }
    if (injector_) {
        injector_->OnSettingsChanged(settings_);
    }
}

void ModLifecycleManager::Uninitialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return;
    }

    if (fetcher_) {
        fetcher_->Stop();
        fetcher_.reset();
    }

    if (injector_) {
        injector_->Shutdown();
        injector_.reset();
    }

    if (cache_) {
        cache_->SetOnUpdatedCallback(nullptr);
        cache_->Clear();
    }

    initialized_ = false;
}

bool ModLifecycleManager::IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

std::shared_ptr<CalendarCache> ModLifecycleManager::GetCache() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_;
}

std::shared_ptr<HttpFetcher> ModLifecycleManager::GetFetcher() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fetcher_;
}

std::shared_ptr<XamlInjector> ModLifecycleManager::GetInjector() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return injector_;
}

ModSettings ModLifecycleManager::GetSettings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

void ModLifecycleManager::SetCustomTransport(std::shared_ptr<IHttpTransport> transport) {
    std::lock_guard<std::mutex> lock(mutex_);
    custom_transport_ = std::move(transport);
    if (fetcher_ && initialized_) {
        fetcher_->Stop();
        fetcher_ = std::make_shared<HttpFetcher>(cache_, custom_transport_);
        fetcher_->Start(settings_);
    }
}

void ModLifecycleManager::SetCustomXamlAdapter(std::shared_ptr<IXamlHostAdapter> adapter) {
    std::lock_guard<std::mutex> lock(mutex_);
    custom_adapter_ = std::move(adapter);
    if (injector_ && initialized_) {
        injector_->Shutdown();
        injector_->Initialize(cache_, settings_, custom_adapter_);
    }
}

void ModLifecycleManager::SetSettingsForTesting(const ModSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
    if (fetcher_ && initialized_) {
        fetcher_->UpdateSettings(settings_);
    }
    if (injector_ && initialized_) {
        injector_->OnSettingsChanged(settings_);
    }
}

void ModLifecycleManager::ResetForTesting() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fetcher_) {
        fetcher_->Stop();
        fetcher_.reset();
    }
    if (injector_) {
        injector_->Shutdown();
        injector_.reset();
    }
    if (cache_) {
        cache_->SetOnUpdatedCallback(nullptr);
        cache_->Clear();
    }
    custom_transport_.reset();
    custom_adapter_.reset();
    settings_ = ModSettings();
    initialized_ = false;
}

} // namespace calendar

#ifdef WH_MOD
// Symbol hook declarations for ShellHost.exe calendar view
typedef void (WINAPI *CalendarView_OnSelectedDatesChanged_t)(void* pThis, void* pArgs);
static CalendarView_OnSelectedDatesChanged_t pOriginalCalendarView_OnSelectedDatesChanged = nullptr;

static void WINAPI Hook_CalendarView_OnSelectedDatesChanged(void* pThis, void* pArgs) {
    if (pOriginalCalendarView_OnSelectedDatesChanged) {
        pOriginalCalendarView_OnSelectedDatesChanged(pThis, pArgs);
    }
    Wh_Log(L"CalendarView selected date changed in ShellHost");
    auto injector = calendar::ModLifecycleManager::Instance().GetInjector();
    if (injector) {
        injector->OnDateSelected(injector->GetSelectedDate());
    }
}
#endif

// Windhawk mod entry points
extern "C" __declspec(dllexport) BOOL Wh_ModInit() {
    Wh_Log(L"Teams Calendar Flyout initializing");
#ifdef WH_MOD
    const WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {
            {
                L"private: void __cdecl winrt::Windows::UI::Xaml::Controls::CalendarView::OnSelectedDatesChanged(struct winrt::Windows::UI::Xaml::Controls::CalendarViewSelectedDatesChangedEventArgs const &)",
                L"?OnSelectedDatesChanged@CalendarView@Controls@Xaml@UI@winrt@@AEAAXAEBUCalendarViewSelectedDatesChangedEventArgs@2345@@Z"
            },
            &pOriginalCalendarView_OnSelectedDatesChanged,
            Hook_CalendarView_OnSelectedDatesChanged,
            true // optional
        }
    };
    WindhawkUtils::HookSymbols(GetModuleHandleW(nullptr), hooks, ARRAYSIZE(hooks));
#endif
    return calendar::ModLifecycleManager::Instance().Initialize() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) void Wh_ModSettingsChanged() {
    Wh_Log(L"Teams Calendar Flyout settings changed");
    calendar::ModLifecycleManager::Instance().OnSettingsChanged();
}

extern "C" __declspec(dllexport) void Wh_ModUninit() {
    Wh_Log(L"Teams Calendar Flyout uninitializing");
    calendar::ModLifecycleManager::Instance().Uninitialize();
}


