#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>
#include <cctype>
#include <compare>
#include "calendar_engine/meeting_status.h"

namespace calendar {

inline std::string_view TrimWhitespace(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

inline bool EqualIgnoreCaseTrimmed(std::string_view a, std::string_view b) {
    a = TrimWhitespace(a);
    b = TrimWhitespace(b);
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

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
