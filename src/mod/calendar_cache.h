#pragma once

#include "calendar_engine/ics_parser.h"
#include <vector>
#include <string>
#include <string_view>
#include <chrono>
#include <shared_mutex>

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

private:
    mutable std::shared_mutex mutex_;
    std::vector<CalendarEvent> events_;
    FetchStatus status_{FetchStatus::Idle};
    std::string last_error_;
    std::chrono::system_clock::time_point last_updated_time_{};
};

} // namespace calendar
