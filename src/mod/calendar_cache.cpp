#include "mod/calendar_cache.h"
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
        if (!show_past_events && ev.temporal_status == MeetingTemporalStatus::Past) {
            continue;
        }
        result.push_back(ev);
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
