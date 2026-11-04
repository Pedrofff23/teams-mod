#pragma once

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
