#pragma once

#include "calendar_engine/ics_parser.h"
#include "calendar_engine/meeting_status.h"
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
    std::string card_bg;
    std::string card_border;
    std::string primary_text_color;
    std::string secondary_text_color;
    std::string accent_color;
    bool is_all_day{false};
    int64_t start_unix{0};
    int64_t end_unix{0};

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
