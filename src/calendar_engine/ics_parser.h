#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>
#include <compare>
#include "calendar_engine/meeting_status.h"

namespace calendar {

struct CalendarDate {
    int year{0};
    int month{0};   // 1 - 12
    int day{0};     // 1 - 31

    auto operator<=>(const CalendarDate&) const = default;
    bool operator==(const CalendarDate&) const = default;

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

struct CalendarEvent {
    std::string uid;
    std::string summary;
    std::string description;
    std::string location;
    CalendarDateTime start_time;
    CalendarDateTime end_time;
    bool is_all_day{false};
    EventStatus status{EventStatus::Confirmed};
    std::string teams_url;
    bool is_teams_meeting{false};
    std::string teams_join_url;
    MeetingTemporalStatus temporal_status{MeetingTemporalStatus::Future};

    // Returns whether this event occurs on the given date (based on local start/end times)
    bool occurs_on(const CalendarDate& target_date) const;
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
