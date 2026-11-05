#include "ui/agenda_view_model.h"
#include "mod/calendar_cache.h"
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <array>
#include <string_view>

namespace calendar {

namespace {

int GetDayOfWeek(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static const char* kDayNames[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

static const char* kMonthNames[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

} // namespace

ThemePalette ThemePalette::ForTheme(ThemeMode mode) {
    ThemePalette p;
    switch (mode) {
    case ThemeMode::Light:
        p.card_background = "#FFFFFF";
        p.card_border = "#E5E5E5";
        p.primary_text = "#1A1A1A";
        p.secondary_text = "#5C5C5C";
        p.teams_accent = "#464EB8";
        p.teams_accent_hover = "#5B63D3";
        p.teams_accent_pressed = "#3B429F";
        p.live_badge_bg = "#107C41";
        p.live_badge_fg = "#FFFFFF";
        p.soon_badge_bg = "#D83B01";
        p.soon_badge_fg = "#FFFFFF";
        p.upcoming_badge_bg = "#0078D4";
        p.upcoming_badge_fg = "#FFFFFF";
        p.past_badge_bg = "#797775";
        p.past_badge_fg = "#FFFFFF";
        p.empty_text = "#616161";
        p.header_text = "#242424";
        break;

    case ThemeMode::HighContrast:
        p.card_background = "#000000";
        p.card_border = "#FFFFFF";
        p.primary_text = "#FFFFFF";
        p.secondary_text = "#FFFF00";
        p.teams_accent = "#00FFFF";
        p.teams_accent_hover = "#33FFFF";
        p.teams_accent_pressed = "#00CCCC";
        p.live_badge_bg = "#008000";
        p.live_badge_fg = "#FFFFFF";
        p.soon_badge_bg = "#FFA500";
        p.soon_badge_fg = "#000000";
        p.upcoming_badge_bg = "#00FFFF";
        p.upcoming_badge_fg = "#000000";
        p.past_badge_bg = "#808080";
        p.past_badge_fg = "#FFFFFF";
        p.empty_text = "#FFFFFF";
        p.header_text = "#FFFFFF";
        break;

    case ThemeMode::Dark:
    default:
        p.card_background = "#202020";
        p.card_border = "#383838";
        p.primary_text = "#FFFFFF";
        p.secondary_text = "#CCCCCC";
        p.teams_accent = "#6264A7";
        p.teams_accent_hover = "#7B7DC4";
        p.teams_accent_pressed = "#51538C";
        p.live_badge_bg = "#107C41";
        p.live_badge_fg = "#FFFFFF";
        p.soon_badge_bg = "#D83B01";
        p.soon_badge_fg = "#FFFFFF";
        p.upcoming_badge_bg = "#0078D4";
        p.upcoming_badge_fg = "#FFFFFF";
        p.past_badge_bg = "#797775";
        p.past_badge_fg = "#E0E0E0";
        p.empty_text = "#9E9E9E";
        p.header_text = "#E0E0E0";
        break;
    }
    return p;
}

std::string AgendaPresenter::FormatTimeRange(const CalendarDateTime& start, const CalendarDateTime& end, bool is_all_day) {
    if (is_all_day) {
        return "All Day";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d - %02d:%02d",
                  start.time.hour, start.time.minute,
                  end.time.hour, end.time.minute);
    return std::string(buf);
}

std::string AgendaPresenter::FormatDateHeader(const CalendarDate& date) {
    if (date.year <= 0 || date.month < 1 || date.month > 12 || date.day < 1 || date.day > 31) {
        return date.to_string();
    }
    int dow = GetDayOfWeek(date.year, date.month, date.day);
    const char* day_name = (dow >= 0 && dow < 7) ? kDayNames[dow] : "";
    const char* month_name = (date.month >= 1 && date.month <= 12) ? kMonthNames[date.month - 1] : "";

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s, %s %d, %04d", day_name, month_name, date.day, date.year);
    return std::string(buf);
}

AgendaItemViewModel AgendaItemViewModel::FromCalendarEvent(
    const CalendarEvent& event,
    ThemeMode theme,
    const CalendarDateTime& reference_time,
    bool enable_join
) {
    AgendaItemViewModel vm;
    vm.uid = event.uid;
    vm.title = event.summary;
    vm.location = event.location;
    vm.is_teams = event.is_teams_meeting;
    vm.join_url = event.teams_join_url;
    vm.enable_join_button = enable_join;
    vm.is_all_day = event.is_all_day;
    vm.start_unix = event.start_time.to_unix_timestamp();
    vm.end_unix = event.end_time.to_unix_timestamp();

    // Recompute or verify temporal status relative to reference_time
    vm.temporal_status = MeetingStatusCalculator::CalculateStatus(event, reference_time);

    vm.palette = ThemePalette::ForTheme(theme);

    vm.formatted_time_range = AgendaPresenter::FormatTimeRange(event.start_time, event.end_time, event.is_all_day);

    switch (vm.temporal_status) {
    case MeetingTemporalStatus::Live:
        vm.status_badge_label = "Live Now";
        vm.status_badge_bg = vm.palette.live_badge_bg;
        vm.status_badge_fg = vm.palette.live_badge_fg;
        break;
    case MeetingTemporalStatus::Soon:
        vm.status_badge_label = "Starting Soon";
        vm.status_badge_bg = vm.palette.soon_badge_bg;
        vm.status_badge_fg = vm.palette.soon_badge_fg;
        break;
    case MeetingTemporalStatus::Past:
        vm.status_badge_label = "Past";
        vm.status_badge_bg = vm.palette.past_badge_bg;
        vm.status_badge_fg = vm.palette.past_badge_fg;
        break;
    case MeetingTemporalStatus::Future:
    default:
        vm.status_badge_label = "Upcoming";
        vm.status_badge_bg = vm.palette.upcoming_badge_bg;
        vm.status_badge_fg = vm.palette.upcoming_badge_fg;
        break;
    }

    return vm;
}

size_t AgendaDayViewModel::teams_meetings_count() const {
    size_t count = 0;
    for (const auto& item : items) {
        if (item.is_teams) {
            ++count;
        }
    }
    return count;
}

AgendaDayViewModel AgendaPresenter::PresentEvents(
    const std::vector<CalendarEvent>& events,
    const CalendarDate& date,
    ThemeMode theme,
    const CalendarDateTime& reference_time,
    bool show_past_events,
    bool enable_join_button
) {
    AgendaDayViewModel day_vm;
    day_vm.selected_date = date;
    day_vm.formatted_date_header = FormatDateHeader(date);
    day_vm.theme = theme;

    std::vector<AgendaItemViewModel> vms;
    for (const auto& ev : events) {
        if (!ev.occurs_on(date)) {
            continue;
        }

        CalendarEvent instance = ev.rrule.is_recurring() ? ev.generate_instance_for_date(date) : ev;

        AgendaItemViewModel vm = AgendaItemViewModel::FromCalendarEvent(
            instance, theme, reference_time, enable_join_button
        );

        if (!show_past_events && vm.temporal_status == MeetingTemporalStatus::Past) {
            continue;
        }

        vms.push_back(std::move(vm));
    }

    // Chronological sorting:
    // 1. All-day events first
    // 2. Timed events sorted by start_unix
    // 3. Ties broken by title
    std::sort(vms.begin(), vms.end(), [](const AgendaItemViewModel& a, const AgendaItemViewModel& b) {
        if (a.is_all_day != b.is_all_day) {
            return a.is_all_day > b.is_all_day; // all-day comes first
        }
        if (a.start_unix != b.start_unix) {
            return a.start_unix < b.start_unix;
        }
        return a.title < b.title;
    });

    // Deduplicate identical occurrences / duplicate invites
    std::vector<AgendaItemViewModel> deduped_vms;
    deduped_vms.reserve(vms.size());
    for (auto& vm : vms) {
        bool duplicate = false;
        for (auto& existing : deduped_vms) {
            bool same_time = (existing.start_unix == vm.start_unix && existing.end_unix == vm.end_unix && existing.is_all_day == vm.is_all_day);
            bool same_uid = (!vm.uid.empty() && existing.uid == vm.uid);
            bool same_summary = EqualIgnoreCaseTrimmed(existing.title, vm.title);
            bool same_join = (!vm.join_url.empty() && existing.join_url == vm.join_url);

            if (same_uid || (same_time && same_summary) || (same_time && same_join)) {
                if (!existing.is_teams && vm.is_teams) {
                    existing.is_teams = true;
                    existing.join_url = vm.join_url;
                }
                if (existing.location.empty() && !vm.location.empty()) {
                    existing.location = vm.location;
                }
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            deduped_vms.push_back(std::move(vm));
        }
    }

    day_vm.items = std::move(deduped_vms);
    return day_vm;
}

AgendaDayViewModel AgendaPresenter::PresentDay(
    const CalendarCache& cache,
    const CalendarDate& date,
    ThemeMode theme,
    const CalendarDateTime& reference_time,
    bool show_past_events,
    bool enable_join_button
) {
    auto events = cache.GetEventsForDate(date, /*show_past_events=*/true);
    return PresentEvents(events, date, theme, reference_time, show_past_events, enable_join_button);
}

} // namespace calendar
