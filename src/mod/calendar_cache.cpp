#include "mod/calendar_cache.h"
#include <algorithm>
#include <cctype>
#include <string_view>
#include <unordered_map>

namespace calendar {

namespace {

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

} // namespace

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
    std::vector<CalendarEvent> matched;
    std::unordered_map<std::string, size_t> uid_map;

    for (const auto& ev : events_) {
        if (!ev.occurs_on(target_date)) {
            continue;
        }
        CalendarEvent instance = ev.rrule.is_recurring() ? ev.generate_instance_for_date(target_date) : ev;
        if (!show_past_events && instance.temporal_status == MeetingTemporalStatus::Past) {
            continue;
        }

        if (!ev.uid.empty()) {
            auto it = uid_map.find(ev.uid);
            if (it != uid_map.end()) {
                // If existing is recurring series master and new instance is an explicit instance/exception, prefer explicit instance
                if (matched[it->second].rrule.is_recurring() && !ev.rrule.is_recurring()) {
                    matched[it->second] = std::move(instance);
                } else {
                    if (matched[it->second].teams_join_url.empty() && !instance.teams_join_url.empty()) {
                        matched[it->second].teams_join_url = instance.teams_join_url;
                        matched[it->second].is_teams_meeting = true;
                    }
                    if (matched[it->second].location.empty() && !instance.location.empty()) {
                        matched[it->second].location = instance.location;
                    }
                }
                continue;
            }
            uid_map[ev.uid] = matched.size();
        }
        matched.push_back(std::move(instance));
    }

    // Sort chronologically
    std::sort(matched.begin(), matched.end(), [](const CalendarEvent& a, const CalendarEvent& b) {
        if (a.start_time != b.start_time) {
            return a.start_time < b.start_time;
        }
        if (a.end_time != b.end_time) {
            return a.end_time < b.end_time;
        }
        return a.uid < b.uid;
    });

    // Deduplicate by (start_time, end_time, summary / join_url)
    std::vector<CalendarEvent> deduped;
    deduped.reserve(matched.size());
    for (auto& ev : matched) {
        bool duplicate = false;
        for (auto& existing : deduped) {
            bool same_time = (existing.start_time == ev.start_time && existing.end_time == ev.end_time);
            bool same_summary = EqualIgnoreCaseTrimmed(existing.summary, ev.summary);
            bool same_join = (!ev.teams_join_url.empty() && existing.teams_join_url == ev.teams_join_url);

            if ((same_time && same_summary) || (same_time && same_join)) {
                if (existing.teams_join_url.empty() && !ev.teams_join_url.empty()) {
                    existing.teams_join_url = ev.teams_join_url;
                    existing.is_teams_meeting = true;
                }
                if (existing.location.empty() && !ev.location.empty()) {
                    existing.location = ev.location;
                }
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            deduped.push_back(std::move(ev));
        }
    }

    return deduped;
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
