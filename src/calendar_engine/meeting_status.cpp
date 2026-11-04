#include "calendar_engine/meeting_status.h"
#include "calendar_engine/ics_parser.h"
#include "calendar_engine/teams_extractor.h"

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
    std::string teams_url = TeamsExtractor::ExtractFromEvent(event);

    if (!teams_url.empty()) {
        event.is_teams_meeting = true;
        event.teams_join_url = teams_url;
        event.teams_url = teams_url;
    } else {
        event.is_teams_meeting = false;
        event.teams_join_url = "";
        event.teams_url = "";
    }

    // 2. Temporal status calculation
    event.temporal_status = CalculateStatus(event, reference_time);
}

void MeetingStatusCalculator::EnrichEvents(std::vector<CalendarEvent>& events, const CalendarDateTime& reference_time) {
    for (auto& event : events) {
        EnrichEvent(event, reference_time);
    }
}

} // namespace calendar
