#include "calendar_engine/ics_parser.h"
#include "calendar_engine/teams_extractor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <unordered_map>

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
// CalendarEvent
// ============================================================================

bool CalendarEvent::occurs_on(const CalendarDate& target_date) const {
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
                current_event.teams_url = detected_teams;
            } else {
                current_event.is_teams_meeting = false;
                current_event.teams_join_url = "";
                current_event.teams_url = "";
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
        } else if (prop_name == "URL" || prop_name == "X-MICROSOFT-SKYPETEAMSMEETINGURL") {
            current_event.teams_join_url = Trim(prop_value);
            current_event.teams_url = current_event.teams_join_url;
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
            matched.push_back(ev);
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
