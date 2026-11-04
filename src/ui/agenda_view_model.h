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
